#pragma once

// LlamaVLM：基于 llama.cpp + mtmd 的多模态模型封装。
// - 模型与 mmproj 路径在构造时给定；
// - 每次 generate 为独立对话（清空 KV）；
// - ChatML 模板（InternVL3 系列使用 <|im_start|>/<|im_end|>）；
// - 非线程安全：仅 VLM Worker 单线程调用。

#include "model/model.hpp"

struct llama_model;
struct llama_context;
struct llama_vocab;
struct llama_sampler;
struct mtmd_context;

namespace model {

struct VLMParams {
    std::string model_path;   // 主模型 GGUF
    std::string mmproj_path;  // 视觉投影器 GGUF
    int32_t n_ctx = 4096;
    int32_t n_batch = 256;
    int32_t n_threads = 0;          // 0 = 自动
    int32_t n_gpu_layers = 0;       // MVP 使用 CPU
    uint32_t seed = 42;
    float temp = 0.0f;              // 感知事实需要可复现，默认 greedy
    int max_tokens = 256;
};

class LlamaVLM : public Model {
public:
    explicit LlamaVLM(const VLMParams& params);
    ~LlamaVLM() override;

    LlamaVLM(const LlamaVLM&) = delete;
    LlamaVLM& operator=(const LlamaVLM&) = delete;

    bool is_loaded() const { return ok_; }
    const std::string& last_error() const { return last_error_; }

    ModelResponse generate(const std::vector<Message>& messages,
                           const uint8_t* rgb = nullptr,
                           int width = 0,
                           int height = 0) override;

private:
    void unload();

    VLMParams params_;
    bool ok_ = false;
    std::string last_error_;

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;  // 所有权仍在 model_
    llama_sampler* sampler_ = nullptr;
    mtmd_context* mctx_ = nullptr;
};

}  // namespace model
