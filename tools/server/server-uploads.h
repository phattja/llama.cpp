#pragma once

#include "server-http.h"

#include <mutex>
#include <string>
#include <unordered_map>

// Chat-attachment store for MCP / server tools.
// Default location is the process temp dir; the client may pick any
// writable directory. ttl_hours == 0 means the file is never pruned.
struct server_uploads {
    static constexpr size_t max_bytes = 1024ull * 1024ull * 1024ull; // 1 GiB
    static constexpr int default_ttl_hours = 24;

    server_http_context::handler_t handle_post;
    server_http_context::handler_t handle_delete;
    server_http_context::handler_t handle_list_dirs;
    server_http_context::handler_t handle_mkdir;

    server_uploads();

private:
    struct entry {
        std::string path;
        std::string name;
        std::string mime_type;
        size_t      size = 0;
        int         ttl_hours = default_ttl_hours;
    };

    std::mutex mutex;
    std::unordered_map<std::string, entry> files;
    std::string default_dir;

    void ensure_default_dir();
    void prune_dir(const std::string & dir);
    std::string new_id();
};
