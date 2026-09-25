#include <cstdio>
#include <string>

#include "app/pipeline.hpp"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", m, __LINE__); ++failures; } } while (0)

int main() {
    std::string out;
    CHECK(app::normalize_visual_checklist(
        "entrance: visible; ground_object: cardboard box; person: visible; hazard: no", out),
        "明确纸箱事实应被规范化");
    CHECK(out.find("ground_object: visible, cardboard box") != std::string::npos,
          "纸箱应规范为可见地面物体");
    CHECK(out.find("hazard: not visible") != std::string::npos, "no 应规范为 not visible");

    CHECK(app::normalize_visual_checklist(
        "entrance: yes; ground_object: package; person: courier; hazard: smoke", out),
        "明确实体词应被接受");
    CHECK(!app::normalize_visual_checklist(
        "entrance: visible; ground_object: door; person: visible; hazard: no", out),
        "door 不得被猜成地面物体");
    CHECK(!app::normalize_visual_checklist(
        "entrance: visible; ground_object: person; person: yes; hazard: no", out),
        "person 不得被猜成地面物体");
    CHECK(!app::normalize_visual_checklist(
        "entrance: visible; ground_object: box; person: visible", out),
        "缺字段必须失败");

    if (!failures) { std::fprintf(stderr, "[PASS] visual_checklist_test\n"); return 0; }
    return 1;
}
