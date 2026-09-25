#include "model/llama_text.hpp"

#include <algorithm>
#include <thread>

#include "llama.h"
#include "model/llama_backend.hpp"

namespace model {
namespace {
std::string role_tag(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::Assistant: return "assistant";
        case Role::User: [[fallthrough]];
        case Role::Tool: return "user";
    }
    return "user";
}
}

LlamaText::LlamaText(const TextModelParams& params) : params_(params) {
    acquire_llama_backend();
    backend_acquired_ = true;
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = params_.n_gpu_layers;
    model_ = llama_model_load_from_file(params_.model_path.c_str(), mp);
    if (!model_) { last_error_ = "文本决策模型加载失败: " + params_.model_path; return; }
    vocab_ = llama_model_get_vocab(model_);
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = params_.n_ctx;
    cp.n_batch = params_.n_batch;
    cp.n_threads = params_.n_threads > 0 ? params_.n_threads
                                         : (int32_t)std::thread::hardware_concurrency();
    cp.n_threads_batch = cp.n_threads;
    ctx_ = llama_init_from_model(model_, cp);
    if (!ctx_) { last_error_ = "文本决策模型 context 创建失败"; unload(); return; }
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    sampler_ = llama_sampler_chain_init(sp);
    if (params_.temp <= 0.0f) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(params_.temp));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(params_.seed));
    }
    ok_ = true;
}

void LlamaText::unload() {
    if (sampler_) { llama_sampler_free(sampler_); sampler_ = nullptr; }
    if (ctx_) { llama_free(ctx_); ctx_ = nullptr; }
    if (model_) { llama_model_free(model_); model_ = nullptr; vocab_ = nullptr; }
}

LlamaText::~LlamaText() {
    unload();
    if (backend_acquired_) release_llama_backend();
}

ModelResponse LlamaText::generate(const std::vector<Message>& messages,
                                  const uint8_t*, int, int) {
    ModelResponse out;
    if (!ok_) return out;
    std::string prompt;
    for (const auto& m : messages)
        prompt += "<|im_start|>" + role_tag(m.role) + "\n" + m.text + "<|im_end|>\n";
    prompt += "<|im_start|>assistant\n";

    llama_memory_clear(llama_get_memory(ctx_), true);
    llama_sampler_reset(sampler_);
    int32_t count = -llama_tokenize(vocab_, prompt.data(), (int32_t)prompt.size(),
                                    nullptr, 0, false, true);
    if (count <= 0 || count >= params_.n_ctx) {
        last_error_ = "文本决策提示 token 数无效或超出 context";
        return out;
    }
    std::vector<llama_token> tokens((size_t)count);
    if (llama_tokenize(vocab_, prompt.data(), (int32_t)prompt.size(), tokens.data(),
                       count, false, true) < 0) {
        last_error_ = "文本决策提示 tokenize 失败";
        return out;
    }
    for (int32_t off = 0; off < count; off += params_.n_batch) {
        const int32_t n = std::min(params_.n_batch, count - off);
        if (llama_decode(ctx_, llama_batch_get_one(tokens.data() + off, n)) != 0) {
            last_error_ = "文本决策提示 decode 失败";
            return out;
        }
    }
    for (int i = 0; i < params_.max_tokens; ++i) {
        llama_token id = llama_sampler_sample(sampler_, ctx_, -1);
        if (llama_vocab_is_eog(vocab_, id)) break;
        char piece[512];
        const int32_t n = llama_token_to_piece(vocab_, id, piece, sizeof(piece), 0, false);
        if (n <= 0) break;
        out.text.append(piece, (size_t)n);
        const size_t stop = out.text.find("<|im_end|>");
        if (stop != std::string::npos) { out.text.erase(stop); break; }
        if (llama_decode(ctx_, llama_batch_get_one(&id, 1)) != 0) break;
        ++out.n_tokens;
    }
    return out;
}

}  // namespace model
