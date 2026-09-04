#include "server-uploads.h"

#include "server-common.h"
#include "base64.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <filesystem>

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
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
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

static std::string strip_data_url(const std::string & data) {
    const auto comma = data.find(',');
    if (data.rfind("data:", 0) == 0 && comma != std::string::npos) {
        return data.substr(comma + 1);
    }
    return data;
}

static bool valid_id(const std::string & id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (unsigned char c : id) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

std::string server_uploads::new_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << dist(rng) << dist(rng);
    return ss.str();
}

void server_uploads::ensure_dir() {
    if (dir.empty()) {
        dir = (fs::temp_directory_path() / "llama-server-uploads").string();
    }
    fs::create_directories(dir);
}

void server_uploads::prune_locked() {
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(ttl_hours);

    std::error_code ec;
    for (auto it = files.begin(); it != files.end(); ) {
        const auto ftime = fs::last_write_time(it->second.path, ec);
        if (ec || ftime < cutoff) {
            fs::remove(it->second.path, ec);
            it = files.erase(it);
        } else {
            ++it;
        }
    }

    if (!fs::exists(dir)) {
        return;
    }

    for (const auto & p : fs::directory_iterator(dir, ec)) {
        if (ec || !p.is_regular_file()) {
            continue;
        }
        const auto ftime = fs::last_write_time(p.path(), ec);
        if (!ec && ftime < cutoff) {
            fs::remove(p.path(), ec);
        }
    }
}

server_uploads::server_uploads() {
    handle_post = [this](const server_http_req & req) -> server_http_res_ptr {
        auto res = std::make_unique<server_http_res>();
        try {
            ensure_dir();

            std::string name;
            std::string mime_type = "application/octet-stream";
            raw_buffer bytes;

            if (!req.files.empty()) {
                const auto & file = req.files.begin()->second;
                name = file.filename;
                if (!file.content_type.empty()) {
                    mime_type = file.content_type;
                }
                bytes = file.data;
            } else {
                json body = json::parse(req.body.empty() ? "{}" : req.body);
                name = json_value(body, "name", std::string("file"));
                mime_type = json_value(body, "mime_type", json_value(body, "mimeType", mime_type));
                const std::string encoded = strip_data_url(json_value(body, "data", std::string()));
                if (encoded.empty()) {
                    throw std::invalid_argument("missing file data");
                }
                const std::string decoded = base64::decode(encoded);
                bytes.assign(decoded.begin(), decoded.end());
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

            std::string id;
            std::string path;
            {
                std::lock_guard<std::mutex> lock(mutex);
                prune_locked();
                id = new_id();
                path = (fs::path(dir) / (id + "_" + sanitize_filename(name))).string();
                files[id] = entry{path, name, mime_type, bytes.size()};
            }

            std::ofstream out(path, std::ios::binary);
            if (!out) {
                throw std::runtime_error("failed to write upload");
            }
            out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize) bytes.size());
            out.close();

            SRV_INF("upload %s -> %s (%zu bytes)\n", name.c_str(), path.c_str(), bytes.size());

            res->data = safe_json_to_str({
                {"id",        id},
                {"name",      name},
                {"path",      path},
                {"mime_type", mime_type},
                {"size",      (int64_t) bytes.size()},
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
                if (it == files.end()) {
                    res->status = 404;
                    res->data   = safe_json_to_str(format_error_response("upload not found", ERROR_TYPE_NOT_FOUND));
                    return res;
                }
                path = it->second.path;
                files.erase(it);
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
}
