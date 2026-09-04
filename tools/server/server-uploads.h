#pragma once

#include "server-http.h"

#include <mutex>
#include <string>
#include <unordered_map>

// Temporary chat-attachment store for MCP / server tools.
// Files live under the process temp dir and are pruned after a TTL.
struct server_uploads {
    static constexpr size_t max_bytes = 64ull * 1024ull * 1024ull; // 64 MiB
    static constexpr int ttl_hours    = 24;

    server_http_context::handler_t handle_post;
    server_http_context::handler_t handle_delete;

    server_uploads();

private:
    struct entry {
        std::string path;
        std::string name;
        std::string mime_type;
        size_t      size = 0;
    };

    std::mutex mutex;
    std::unordered_map<std::string, entry> files;
    std::string dir;

    void ensure_dir();
    void prune_locked();
    std::string new_id();
};
