// Skill 加载与提示注入验收；真实 Qwen 路由行为由 decision_model_test 覆盖。
#include <cstdio>
#include <string>

#include "skill/skill_loader.hpp"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { std::fprintf(stderr, "[FAIL] %s (line %d)\n", m, __LINE__); ++failures; } } while (0)

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: skill_test <skill_a.md> <skill_b.md>\n"); return 2; }
    skill::Skill a, b; std::string error;
    CHECK(skill::load_skill(argv[1], a, error), "加载 Skill A");
    CHECK(skill::load_skill(argv[2], b, error), "加载 Skill B");
    if (failures) return 1;
    const auto pa = skill::build_system_prompt(a, {"push_frame"});
    const auto pb = skill::build_system_prompt(b, {"push_frame"});
    CHECK(pa.find(a.content) != std::string::npos, "Skill A 全文进入提示");
    CHECK(pb.find(b.content) != std::string::npos, "Skill B 全文进入提示");
    CHECK(pa != pb, "更换 Skill 会改变决策提示");
    CHECK(pa.find("\"name\":\"push_frame\"") != std::string::npos, "仅声明 push_frame");
    CHECK(pa.find("notify") == std::string::npos && pa.find("save_event") == std::string::npos,
          "旧工具不得进入提示");
    CHECK(pa.find("true rule must never be returned as final") != std::string::npos,
          "命中规则不得 final");
    if (!failures) { std::fprintf(stderr, "[PASS] skill_test\n"); return 0; }
    return 1;
}
