#pragma once

// llama.cpp 全局后端由多个模型实例共享；引用计数避免双模型重复释放。

namespace model {

void acquire_llama_backend();
void release_llama_backend();

}  // namespace model
