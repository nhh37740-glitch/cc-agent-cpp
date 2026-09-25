#pragma once

// 纯文本决策模型：读取 Skill、工具定义和视觉模型的事实描述，输出严格 JSON。

#include "model/model.hpp"

struct llama_model;
struct llama_context;
struct llama_vocab;
struct llama_sampler;

namespace model {

struct TextModelParams {
    std::string model_path;
    int32_t n_ctx = 4096;
    int32_t n_batch = 512;
    int32_t n_threads = 0;
    int32_t n_gpu_layers = 0;
    int32_t max_tokens = 256;
    float temp = 0.0f;
    uint32_t seed = 42;
};

class LlamaText final : public Model {
public:
    explicit LlamaText(const TextModelParams& params);
    ~LlamaText() override;
    LlamaText(const LlamaText&) = delete;
    LlamaText& operator=(const LlamaText&) = delete;

    bool is_loaded() const { return ok_; }
    const std::string& last_error() const { return last_error_; }
    ModelResponse generate(const std::vector<Message>& messages,
                           const uint8_t* rgb = nullptr,
                           int width = 0, int height = 0) override;

private:
    void unload();
    TextModelParams params_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    bool backend_acquired_ = false;
    bool ok_ = false;
    std::string last_error_;
};

}  // namespace model
