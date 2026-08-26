#pragma once

// 模型接口与消息类型。Model 抽象仅保留 generate；
// 当前唯一实现是 LlamaVLM（llama.cpp + mtmd）。

#include <cstdint>
#include <string>
#include <vector>

namespace model {

enum class Role { System, User, Assistant, Tool };

struct Message {
    Role role = Role::User;
    std::string text;

    // User 消息可携带图像；图像数据在调用 generate 时以 RGB 缓冲传入，
    // has_image 标记该条消息的文本中应插入媒体标记。
    bool has_image = false;
};

struct ModelResponse {
    std::string text;      // 生成的原始文本
    int n_tokens = 0;      // 生成的 token 数
};

class Model {
public:
    virtual ~Model() = default;

    // 单次生成：messages 为本轮对话上下文（每次调用都是独立对话）。
    // rgb 非空时，标记了 has_image 的消息位置会插入该图像。
    virtual ModelResponse generate(const std::vector<Message>& messages,
                                   const uint8_t* rgb = nullptr,
                                   int width = 0,
                                   int height = 0) = 0;
};

}  // namespace model
