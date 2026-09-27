// CLI 行为验收：
// - --help / -h 打印用法并以 0 退出；
// - 非法参数（未知参数、缺值、queue-capacity 越界/非法、负数、非数字）以 2 退出且不崩溃；
// - 视频文件不存在时以非 0 退出（1），不再误报成功。
// 用法：cli_args_test <edge_agent.exe 路径>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

struct RunResult {
    int exit_code = -1;
    double secs = 0.0;
    std::string output;
};

// 执行主程序并捕获输出（由各平台 shell 重定向到临时文件）
RunResult run_agent(const std::string& exe, const std::vector<std::string>& args) {
    std::string cmd = "\"" + exe + "\"";
    for (const auto& a : args) cmd += " " + a;
    const std::string out_file =
        (std::filesystem::temp_directory_path() / "edge_cli_out.txt").string();
    cmd += " > \"" + out_file + "\" 2>&1";

    const auto t0 = std::chrono::steady_clock::now();
    // Windows 的 system 经 cmd.exe 时需要额外的外层引号；POSIX 直接交给 /bin/sh。
#if defined(_WIN32)
    const int raw_rc = std::system(("\"" + cmd + "\"").c_str());
    const int exit_code = raw_rc;
#else
    const int raw_rc = std::system(cmd.c_str());
    const int exit_code = raw_rc == -1 ? -1
        : WIFEXITED(raw_rc) ? WEXITSTATUS(raw_rc)
        : 128 + WTERMSIG(raw_rc);
#endif
    RunResult r;
    r.exit_code = exit_code;
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::ifstream f(out_file);
    std::string line;
    while (std::getline(f, line)) r.output += line + "\n";
    return r;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cli_args_test <edge_agent.exe>\n");
        return 2;
    }
    const std::string exe = argv[1];

    // --help / -h → 0，包含完整参数表
    for (const char* flag : {"--help", "-h"}) {
        auto r = run_agent(exe, {flag});
        CHECK(r.exit_code == 0, (std::string(flag) + " 应退出 0").c_str());
        CHECK(r.output.find("--analysis-width") != std::string::npos,
              "帮助信息应含 --analysis-width");
        CHECK(r.output.find("--web-idle") != std::string::npos,
              "帮助信息应含无推理等待页面模式");
    }

    // 无参数 → 2
    CHECK(run_agent(exe, {}).exit_code == 2, "无参数应退出 2");

    // 未知参数 → 2
    CHECK(run_agent(exe, {"--bogus", "--video", "x.mp4"}).exit_code == 2,
          "未知参数应退出 2");

    // 缺参数值 → 2
    CHECK(run_agent(exe, {"--video"}).exit_code == 2, "缺少参数值应退出 2");

    CHECK(run_agent(exe, {"--web-idle"}).exit_code == 2,
          "等待配置模式未指定 web-port 应退出 2");
    CHECK(run_agent(exe, {"--web-idle", "--web-port", "8080", "--video", "x.mp4"}).exit_code == 2,
          "等待配置模式不得同时指定视频");
    CHECK(run_agent(exe, {"--web-idle", "--web-port", "8080", "--model", "vision.gguf"}).exit_code == 2,
          "等待配置模式不得同时指定模型");

    CHECK(run_agent(exe, {"--video", "x.mp4", "--unattended"}).exit_code == 2,
          "无人值守未提供 web-port 应退出 2");
    // 当前 CLI 只接入视觉模型；未接入的参数必须明确拒绝，不能被静默忽略。
    auto decision_arg = run_agent(exe, {"--video", "x.mp4", "--model", "vision.gguf",
                                         "--mmproj", "mm.gguf", "--decision-model", "qwen.gguf"});
    CHECK(decision_arg.exit_code == 2 &&
          decision_arg.output.find("--decision-model") != std::string::npos,
          "未接入的 decision-model 参数应明确拒绝");
    auto skill_arg = run_agent(exe, {"--video", "x.mp4", "--no-vlm",
                                      "--skill", "skills/door-camera.md"});
    CHECK(skill_arg.exit_code == 2 && skill_arg.output.find("--skill") != std::string::npos,
          "未接入的 skill 参数应明确拒绝");

    // queue-capacity 非法值 → 2 且快速结束（不进入管线）
    for (const char* bad : {"0", "3", "9", "-1", "abc", "6x", "999999999999999999999"}) {
        auto r = run_agent(exe, {"--video", "x.mp4", "--no-vlm",
                                 "--queue-capacity", bad});
        std::fprintf(stderr, "capacity=%s -> exit=%d (%.2fs)\n", bad, r.exit_code, r.secs);
        CHECK(r.exit_code == 2,
              (std::string("queue-capacity ") + bad + " 应退出 2").c_str());
        CHECK(r.secs < 2.0, "非法容量应在 2 秒内拒绝（不得崩溃或挂起）");
    }

    // queue-capacity 合法边界 4 和 8 应被接受（视频不存在时因视频错误退出 1，
    // 而不是参数错误 2 —— 以此区分两类失败）
    for (const char* ok : {"4", "8"}) {
        auto r = run_agent(exe, {"--video", "x.mp4", "--no-vlm", "--no-pacing",
                                 "--log", "build/cli_ok.jsonl", "--queue-capacity", ok});
        std::fprintf(stderr, "capacity=%s (missing video) -> exit=%d\n", ok, r.exit_code);
        CHECK(r.exit_code == 1,
              (std::string("合法容量 ") + ok + " 应回退到视频错误(1)，而非参数错误(2)").c_str());
    }

    // 视频不存在 → 1（不再是 0）
    {
        auto r = run_agent(exe, {"--video", "this-file-does-not-exist.mp4",
                                 "--no-vlm", "--no-pacing",
                                 "--log", "build/cli_missing.jsonl"});
        std::fprintf(stderr, "missing video -> exit=%d\n%.60s", r.exit_code,
                     r.output.c_str());
        CHECK(r.exit_code == 1, "打开失败的视频应以退出码 1 结束");
    }

    if (failures == 0) {
        std::fprintf(stderr, "[PASS] cli_args_test\n");
        return 0;
    }
    return 1;
}
