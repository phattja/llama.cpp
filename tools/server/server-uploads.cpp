#include "server-uploads.h"

#include "server-common.h"
#include "base64.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <filesystem>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace fs = std::filesystem;

static std::string sanitize_filename(std::string name) {
    if (name.empty()) {
        return "file";
    }

    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }

    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ' ' || c == '(' || c == ')' || c == '+') {
            out.push_back((char) c);
        } else {
            out.push_back('_');
        }
    }

    if (out.empty() || out[0] == '.') {
        out = "file_" + out;
    }

    if (out.size() > 80) {
        out.resize(80);
    }

    return out;
}

static std::string sanitize_dirname(const std::string & name) {
    if (name.empty() || name == "." || name == "..") {
        throw std::invalid_argument("invalid folder name");
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        throw std::invalid_argument("invalid folder name");
    }
    return sanitize_filename(name);
}

static std::string strip_data_url(const std::string & data) {
    const auto comma = data.find(',');
    if (data.rfind("data:", 0) == 0 && comma != std::string::npos) {
        return data.substr(comma + 1);
    }
    return data;
}

static bool valid_id(const std::string & id) {
    if (id.empty() || id.size() > 255) {
        return false;
    }
    for (unsigned char c : id) {
        if (c == '/' || c == '\\' || c == '\0') {
            return false;
        }
    }
    return true;
}

static fs::path weakly_abs(const std::string & path) {
    std::error_code ec;
    fs::path p(path);
    if (p.empty()) {
        throw std::invalid_argument("empty path");
    }
    auto canon = fs::weakly_canonical(p, ec);
    if (ec) {
        canon = fs::absolute(p, ec);
    }
    if (ec) {
        throw std::invalid_argument("invalid path");
    }
    return canon;
}

static bool dir_is_writable(const fs::path & p) {
    std::error_code ec;
    if (!fs::is_directory(p, ec) || ec) {
        return false;
    }
#if defined(_WIN32)
    const auto probe = p / ".llama_wtest";
    {
        std::ofstream out(probe);
        if (!out) {
            return false;
        }
    }
    fs::remove(probe, ec);
    return true;
#else
    return ::access(p.string().c_str(), W_OK) == 0;
#endif
}

void server_uploads::ensure_default_dir() {
    if (default_dir.empty()) {
        default_dir = (fs::temp_directory_path() / "llama-server-uploads").string();
    }
    fs::create_directories(default_dir);
}

void server_uploads::prune_dir(const std::string & dir, int ttl_hours) {
    if (ttl_hours <= 0) {
        return;
    }

    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return;
    }

    const auto now = fs::file_time_type::clock::now();
    const auto max_age = std::chrono::seconds((int64_t) ttl_hours * 3600);

    for (const auto & p : fs::directory_iterator(dir, ec)) {
        if (ec || !p.is_regular_file()) {
            continue;
        }
        const auto path = p.path();
        const auto mtime = fs::last_write_time(path, ec);
        if (ec) {
            continue;
        }
        if (now - mtime < max_age) {
            continue;
        }
        fs::remove(path, ec);
        files.erase(path.filename().string());
    }
}

server_uploads::server_uploads() {
    handle_post = [this](const server_http_req & req) -> server_http_res_ptr {
        auto res = std::make_unique<server_http_res>();
        try {
            ensure_default_dir();

            std::string name;
            std::string mime_type = "application/octet-stream";
            std::string dest_dir  = default_dir;
            int ttl_hours         = default_ttl_hours;
            raw_buffer bytes;

            json body = json::object();
            if (!req.body.empty() && req.files.empty()) {
                body = json::parse(req.body);
            }

            if (!req.files.empty()) {
                const auto & file = req.files.begin()->second;
                name = file.filename;
                if (!file.content_type.empty()) {
                    mime_type = file.content_type;
                }
                bytes = file.data;
                if (!req.body.empty()) {
                    try {
                        body = json::parse(req.body);
                    } catch (...) {
                    }
                }
            } else {
                name = json_value(body, "name", std::string("file"));
                mime_type = json_value(body, "mime_type", json_value(body, "mimeType", mime_type));
                const std::string encoded = strip_data_url(json_value(body, "data", std::string()));
                if (encoded.empty()) {
                    throw std::invalid_argument("missing file data");
                }
                const std::string decoded = base64::decode(encoded);
                bytes.assign(decoded.begin(), decoded.end());
            }

            const std::string dir_opt = json_value(body, "dir", std::string());
            if (!dir_opt.empty()) {
                dest_dir = weakly_abs(dir_opt).string();
            }
            ttl_hours = json_value(body, "ttl_hours", json_value(body, "ttlHours", ttl_hours));
            if (ttl_hours < 0) {
                throw std::invalid_argument("ttl_hours must be >= 0");
            }

            if (name.empty()) {
                name = "file";
            }
            if (bytes.empty()) {
                throw std::invalid_argument("empty file");
            }
            if (bytes.size() > max_bytes) {
                throw std::invalid_argument("file too large");
            }

            fs::create_directories(dest_dir);
            if (!dir_is_writable(dest_dir)) {
                throw std::invalid_argument("directory is not writable");
            }

            std::string id;
            std::string path;
            {
                std::lock_guard<std::mutex> lock(mutex);
                prune_dir(dest_dir, ttl_hours);
                id = sanitize_filename(name);
                path = (fs::path(dest_dir) / id).string();
                files[id] = entry{path, name, mime_type, bytes.size(), ttl_hours};
            }

            std::ofstream out(path, std::ios::binary);
            if (!out) {
                throw std::runtime_error("failed to write upload");
            }
            out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize) bytes.size());
            out.close();

            SRV_INF("upload %s -> %s (%zu bytes, ttl=%d h)\n", name.c_str(), path.c_str(), bytes.size(), ttl_hours);

            res->data = safe_json_to_str({
                {"id",        id},
                {"name",      name},
                {"path",      path},
                {"mime_type", mime_type},
                {"size",      (int64_t) bytes.size()},
                {"ttl_hours", ttl_hours},
            });
        } catch (const std::invalid_argument & e) {
            res->status = 400;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_INVALID_REQUEST));
        } catch (const std::exception & e) {
            SRV_ERR("upload failed: %s\n", e.what());
            res->status = 500;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_SERVER));
        }
        return res;
    };

    handle_delete = [this](const server_http_req & req) -> server_http_res_ptr {
        auto res = std::make_unique<server_http_res>();
        try {
            const std::string id = req.get_param("id");
            if (!valid_id(id)) {
                throw std::invalid_argument("invalid upload id");
            }

            std::string path;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = files.find(id);
                if (it != files.end()) {
                    path = it->second.path;
                    files.erase(it);
                }
            }

            if (path.empty()) {
                res->status = 404;
                res->data   = safe_json_to_str(format_error_response("upload not found", ERROR_TYPE_NOT_FOUND));
                return res;
            }

            std::error_code ec;
            fs::remove(path, ec);
            res->data = safe_json_to_str({{"ok", true}, {"id", id}});
        } catch (const std::invalid_argument & e) {
            res->status = 400;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_INVALID_REQUEST));
        } catch (const std::exception & e) {
            res->status = 500;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_SERVER));
        }
        return res;
    };

    handle_list_dirs = [this](const server_http_req & req) -> server_http_res_ptr {
        auto res = std::make_unique<server_http_res>();
        try {
            ensure_default_dir();
            const std::string requested = req.get_param("path");
            int list_ttl = default_ttl_hours;
            const std::string ttl_s = req.get_param("ttl_hours");
            if (!ttl_s.empty()) {
                list_ttl = std::atoi(ttl_s.c_str());
            }

            json entries = json::array();
            std::string current;
            std::string parent;
            bool writable = false;

            if (requested.empty()) {
                current = "";
                writable = false;
                auto add_root = [&](const fs::path & p) {
                    std::error_code ec;
                    if (!fs::is_directory(p, ec) || ec || !dir_is_writable(p)) {
                        return;
                    }
                    const std::string abs = weakly_abs(p.string()).string();
                    entries.push_back(json{
                        {"name", p.filename().string().empty() ? abs : p.filename().string()},
                        {"path", abs},
                        {"writable", true},
                    });
                };
                add_root(fs::current_path());
                add_root(fs::temp_directory_path());
                add_root(fs::path(default_dir));
#ifdef _WIN32
                const char * home = std::getenv("USERPROFILE");
#else
                const char * home = std::getenv("HOME");
#endif
                if (home && home[0]) {
                    add_root(fs::path(home));
                }
            } else {
                const fs::path cur = weakly_abs(requested);
                if (!fs::is_directory(cur)) {
                    throw std::invalid_argument("not a directory");
                }
                current = cur.string();
                writable = dir_is_writable(cur);
                if (cur.has_parent_path() && cur.parent_path() != cur) {
                    parent = cur.parent_path().string();
                }
                std::error_code ec;
                for (const auto & p : fs::directory_iterator(cur, ec)) {
                    if (ec || !p.is_directory()) {
                        continue;
                    }
                    if (!dir_is_writable(p.path())) {
                        continue;
                    }
                    entries.push_back(json{
                        {"name", p.path().filename().string()},
                        {"path", weakly_abs(p.path().string()).string()},
                        {"writable", true},
                    });
                }
            }

            json files_json = json::array();
            const std::string files_dir = current.empty() ? default_dir : current;
            const bool files_writable = current.empty() ? dir_is_writable(files_dir) : writable;
            if (!files_dir.empty() && files_writable) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    prune_dir(files_dir, list_ttl);
                }
                std::error_code fec;
                for (const auto & p : fs::directory_iterator(files_dir, fec)) {
                    if (fec || !p.is_regular_file()) {
                        continue;
                    }
                    std::error_code sz_ec;
                    const auto sz = fs::file_size(p.path(), sz_ec);
                    files_json.push_back(json{
                        {"name", p.path().filename().string()},
                        {"path", weakly_abs(p.path().string()).string()},
                        {"size", sz_ec ? (int64_t) 0 : (int64_t) sz},
                    });
                }
            }

            res->data = safe_json_to_str({
                {"path",     current},
                {"parent",   parent},
                {"writable", writable},
                {"entries",  entries},
                {"files",    files_json},
            });
        } catch (const std::invalid_argument & e) {
            res->status = 400;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_INVALID_REQUEST));
        } catch (const std::exception & e) {
            res->status = 500;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_SERVER));
        }
        return res;
    };

    handle_mkdir = [this](const server_http_req & req) -> server_http_res_ptr {
        auto res = std::make_unique<server_http_res>();
        try {
            json body = json::parse(req.body.empty() ? "{}" : req.body);
            fs::path dest;
            const std::string direct = json_value(body, "path", std::string());
            if (!direct.empty()) {
                dest = weakly_abs(direct);
            } else {
                const std::string parent = json_value(body, "parent", std::string());
                const std::string name = sanitize_dirname(json_value(body, "name", std::string()));
                if (parent.empty()) {
                    throw std::invalid_argument("missing parent");
                }
                dest = weakly_abs(parent) / name;
            }

            const fs::path parent = dest.parent_path();
            if (!dir_is_writable(parent)) {
                throw std::invalid_argument("parent directory is not writable");
            }
            fs::create_directories(dest);
            if (!dir_is_writable(dest)) {
                throw std::runtime_error("created directory is not writable");
            }

            res->data = safe_json_to_str({
                {"path",     dest.string()},
                {"writable", true},
            });
        } catch (const std::invalid_argument & e) {
            res->status = 400;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_INVALID_REQUEST));
        } catch (const std::exception & e) {
            res->status = 500;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_SERVER));
        }
        return res;
    };

    handle_delete_files = [this](const server_http_req & req) -> server_http_res_ptr {
        auto res = std::make_unique<server_http_res>();
        try {
            json body = json::parse(req.body.empty() ? "{}" : req.body);
            json paths = body.contains("paths") && body["paths"].is_array() ? body["paths"] : json::array();
            if (paths.empty()) {
                throw std::invalid_argument("missing paths");
            }

            json deleted = json::array();
            json failed  = json::array();

            for (const auto & item : paths) {
                const std::string raw = item.is_string() ? item.get<std::string>() : std::string();
                if (raw.empty()) {
                    continue;
                }
                try {
                    const fs::path file = weakly_abs(raw);
                    std::error_code ec;
                    if (!fs::is_regular_file(file, ec) || ec) {
                        throw std::invalid_argument("not a file");
                    }
                    if (!dir_is_writable(file.parent_path())) {
                        throw std::invalid_argument("directory is not writable");
                    }

                    fs::remove(file, ec);
                    if (ec) {
                        throw std::runtime_error(ec.message());
                    }

                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        files.erase(file.filename().string());
                    }

                    deleted.push_back(file.string());
                } catch (const std::exception & e) {
                    failed.push_back({{"path", raw}, {"error", e.what()}});
                }
            }

            res->data = safe_json_to_str({
                {"deleted", deleted},
                {"failed",  failed},
            });
        } catch (const std::invalid_argument & e) {
            res->status = 400;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_INVALID_REQUEST));
        } catch (const std::exception & e) {
            res->status = 500;
            res->data   = safe_json_to_str(format_error_response(e.what(), ERROR_TYPE_SERVER));
        }
        return res;
    };
}
