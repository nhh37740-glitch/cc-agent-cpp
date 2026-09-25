// LlamaVLM 实现。生成流程：
// 1. 把 messages 按 ChatML 拼成提示文本，带图消息处插入 <__media__> 标记；
// 2. mtmd_tokenize 拆分为文本/图像 chunk；
// 3. mtmd_helper_eval_chunks 一次完成文本 decode 与视觉编码；
// 4. 逐 token 采样直到 EOG、长度上限或 ChatML 结束标记。

#include "model/llama_vlm.hpp"

#include "llama.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#include "model/llama_backend.hpp"

#include <stdexcept>
#include <thread>

namespace model {

static const char* kMediaMarker = "<__media__>";

LlamaVLM::LlamaVLM(const VLMParams& params) : params_(params) {
    acquire_llama_backend();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = params_.n_gpu_layers;
    model_ = llama_model_load_from_file(params_.model_path.c_str(), mparams);
    if (!model_) {
        last_error_ = "模型加载失败: " + params_.model_path;
        return;
    }
    vocab_ = llama_model_get_vocab(model_);

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = params_.n_ctx;
    cparams.n_batch = params_.n_batch;
    cparams.n_threads = params_.n_threads > 0 ? params_.n_threads
                                              : (int32_t)std::thread::hardware_concurrency();
    cparams.n_threads_batch = cparams.n_threads;
    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        last_error_ = "llama context 创建失败";
        unload();
        return;
    }

    mtmd_context_params mctx_params = mtmd_context_params_default();
    mctx_params.use_gpu = false;
    mctx_params.print_timings = false;
    mctx_params.warmup = false;
    mctx_params.n_threads = cparams.n_threads;
    mctx_ = mtmd_init_from_file(params_.mmproj_path.c_str(), model_, mctx_params);
    if (!mctx_) {
        last_error_ = "mmproj 加载失败: " + params_.mmproj_path;
        unload();
        return;
    }
    if (!mtmd_support_vision(mctx_)) {
        last_error_ = "mmproj 不支持视觉输入";
        unload();
        return;
    }

    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;
    sampler_ = llama_sampler_chain_init(sparams);
    if (params_.temp <= 0.0f) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
    } else {
        const int32_t n_vocab = llama_vocab_n_tokens(vocab_);
        llama_sampler_chain_add(sampler_,
            llama_sampler_init_penalties(n_vocab, 64, 1.1f, 0.0f, 0.0f));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(0.9f, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(params_.temp));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(params_.seed));
    }

    ok_ = true;
}

void LlamaVLM::unload() {
    if (sampler_) { llama_sampler_free(sampler_); sampler_ = nullptr; }
    if (mctx_) { mtmd_free(mctx_); mctx_ = nullptr; }
    if (ctx_) { llama_free(ctx_); ctx_ = nullptr; }
    if (model_) { llama_model_free(model_); model_ = nullptr; vocab_ = nullptr; }
}

LlamaVLM::~LlamaVLM() {
    unload();
    release_llama_backend();
}

static std::string role_tag(const Message& msg) {
    // Tool 结果以 user 角色回填（ChatML 只有 system/user/assistant）
    switch (msg.role) {
        case Role::System: return "system";
        case Role::User: [[fallthrough]];
        case Role::Tool: return "user";
        case Role::Assistant: return "assistant";
    }
    return "user";
}

ModelResponse LlamaVLM::generate(const std::vector<Message>& messages,
                                 const uint8_t* rgb, int width, int height) {
    ModelResponse result;

    // ---- 1. 拼 ChatML 提示（MVP 约束：每次生成至多一张图） ----
    std::string prompt;
    for (const auto& msg : messages) {
        std::string text = msg.text;
        if (msg.has_image && rgb && text.find(kMediaMarker) == std::string::npos) {
            text += kMediaMarker;  // 兜底：标记缺失时追加到消息末尾
        }
        prompt += "<|im_start|>" + role_tag(msg) + "\n" + text + "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    const size_t n_images = rgb ? 1u : 0u;

    // ---- 2. 清空 KV 与 sampler 历史，保证每次生成为独立对话 ----
    llama_memory_clear(llama_get_memory(ctx_), true);
    llama_sampler_reset(sampler_);  // penalties 等采样器的 recent-token 状态不跨生成残留

    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    mtmd_bitmap* bitmap = nullptr;
    if (n_images > 0 && rgb) {
        bitmap = mtmd_bitmap_init((uint32_t)width, (uint32_t)height, rgb);
    }
    const mtmd_bitmap* bitmaps[1] = {bitmap};
    mtmd_input_text text{prompt.c_str(), prompt.size(), /*add_special*/ false,
                         /*parse_special*/ true};

    int32_t ret = mtmd_tokenize(mctx_, chunks, &text, bitmaps, n_images > 0 ? 1 : 0);
    if (bitmap) mtmd_bitmap_free(bitmap);
    if (ret != 0) {
        last_error_ = "mtmd_tokenize 失败 code=" + std::to_string(ret);
        mtmd_input_chunks_free(chunks);
        result.text = "";
        return result;
    }

    llama_pos n_past = 0;
    ret = mtmd_helper_eval_chunks(mctx_, ctx_, chunks, 0, 0, params_.n_batch,
                                  /*logits_last*/ true, &n_past);
    mtmd_input_chunks_free(chunks);
    if (ret != 0) {
        last_error_ = "eval chunks 失败 code=" + std::to_string(ret);
        result.text = "";
        return result;
    }

    // ---- 4. 逐 token 采样 ----
    std::string gen_text;
    for (int i = 0; i < params_.max_tokens; ++i) {
        llama_token id = llama_sampler_sample(sampler_, ctx_, -1);
        if (llama_vocab_is_eog(vocab_, id)) break;

        char buf[512];
        int32_t n = llama_token_to_piece(vocab_, id, buf, sizeof(buf), 0, false);
        if (n <= 0) break;
        gen_text.append(buf, (size_t)n);

        // ChatML 结束标记：截断并停止
        static const char* kStops[] = {"<|im_end|>", "<|im_start|>"};
        bool stopped = false;
        for (const char* s : kStops) {
            size_t pos = gen_text.find(s);
            if (pos != std::string::npos) {
                gen_text.erase(pos);
                stopped = true;
                break;
            }
        }
        if (stopped) break;

        llama_batch batch = llama_batch_get_one(&id, 1);
        if (llama_decode(ctx_, batch) != 0) break;
        ++result.n_tokens;
    }

    result.text = std::move(gen_text);
    return result;
}

}  // namespace model
