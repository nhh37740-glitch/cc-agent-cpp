#include "model/llama_backend.hpp"

#include <mutex>

#include "llama.h"

namespace model {
namespace {
std::mutex g_backend_mutex;
int g_backend_users = 0;
}

void acquire_llama_backend() {
    std::lock_guard lock(g_backend_mutex);
    if (g_backend_users++ == 0) llama_backend_init();
}

void release_llama_backend() {
    std::lock_guard lock(g_backend_mutex);
    if (g_backend_users > 0 && --g_backend_users == 0) llama_backend_free();
}

}  // namespace model
