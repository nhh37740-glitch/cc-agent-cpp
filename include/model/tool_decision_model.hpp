#pragma once

// 小模型工具决策适配器：底层文本模型只做严格 PUSH/FINAL 二分类，
// 适配器把明确枚举映射为 Agent Loop 所需的结构化 JSON。

#include "model/model.hpp"

namespace model {

class ToolDecisionModel final : public Model {
public:
    ToolDecisionModel(Model& text_model, std::string policy)
        : text_model_(text_model), policy_(std::move(policy)) {}

    ModelResponse generate(const std::vector<Message>& messages,
                           const uint8_t* rgb = nullptr,
                           int width = 0, int height = 0) override;

private:
    Model& text_model_;
    std::string policy_;
};

}  // namespace model
