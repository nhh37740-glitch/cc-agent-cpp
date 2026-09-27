#include <cstdio>
#include <string>

#include "app/pipeline.hpp"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", m, __LINE__); ++failures; } } while (0)

int main() {
    std::string out;
    CHECK(app::normalize_visual_checklist(
        "  门口有一个纸箱和一位快递员。  ", out),
        "非空中文视觉描述应被接受");
    CHECK(out == "门口有一个纸箱和一位快递员。", "视觉描述只去除首尾空白");
    CHECK(app::normalize_visual_checklist(
        "door and person visible", out),
        "当前自然语言描述不依赖旧四字段协议");
    CHECK(out == "door and person visible", "英文描述不应被改写");
    CHECK(!app::normalize_visual_checklist(" \t\n ", out), "空白描述必须拒绝");
    CHECK(!app::normalize_visual_checklist("scene: empty", out), "明确空场景必须拒绝");
    CHECK(!app::normalize_visual_checklist(" SCENE: EMPTY. ", out),
          "空场景标记的大小写和句点不影响拒绝");
    CHECK(app::normalize_visual_checklist("an empty shelf with a box", out),
          "普通句子中的 empty 不等于空场景标记");

    if (!failures) { std::fprintf(stderr, "[PASS] visual_checklist_test\n"); return 0; }
    return 1;
}
