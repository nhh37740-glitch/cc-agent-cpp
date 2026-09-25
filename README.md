# edge-agent：本地多模态边缘 Agent Runtime（MVP）

使用 C++20、FFmpeg 与 llama.cpp（mtmd）构建的本地多模态 Agent：
以持续视频流为输入，通过低成本事件筛选和有界队列控制资源，
由 InternVL3-1B-Instruct 只提取可见事实，再由 Qwen2.5-1.5B-Instruct
读取 Skill、工具定义和视觉事实，决定是否推送关键帧。

## 架构

```
本地 MP4（模拟实时摄像头，按 PTS 单向读取，不 seek）
        ↓  Video Worker (jthread A)
FFmpeg 解码 → FrameFilter（时间采样 + Y 平面降采样帧差）
        ↓  候选帧确定后才转 RGB 并降采样
BoundedQueue<CandidateFrame>（容量 4~8，满时 drop_oldest）
        ↓  VLM Worker (jthread B)
InternVL3-1B + mtmd → 严格视觉事实 JSON
        ↓
Qwen2.5-1.5B → 严格 PUSH / FINAL 路由 → Agent Loop
        ↓
ToolRegistry 唯一白名单工具 push_frame(summary)
        ↓
成功推送的关键帧与分析 → 局域网只读 Web 面板
```

核心约束：

- 视频单向流式读取，禁止 seek / 二遍扫描 / 整段缓存；管线内存 O(1)
- Video Worker 绝不等待 VLM；队列满时 drop_oldest（摄像头语义）
- Skill 用 Markdown 定义，更换任务无需重编译；但权限只认 C++ 白名单
- Qwen 路由只接受完整的 `PUSH` / `FINAL`；适配器生成结构化 `tool_call` / `final`，不从自由文本猜工具
- Agent Loop 最多 3 次模型生成；无后续预算时不执行新工具调用，
  返回 `STEP_LIMIT_REACHED` 并记录 `reason=no_followup_generation_budget`，不伪造 ToolResult
- 全部运行事件写入 JSONL 日志

## 目录结构

```
CMakeLists.txt          # 依赖检测（缺失即 FATAL_ERROR）、全部目标与测试
include/ src/           # 模块源码（video/filter/queue/model/skill/agent/app）
tests/                  # 自动化测试（CTest 注册，含真实推理冒烟测试）
skills/                 # door-camera.md、pet-watch.md（Skill 示例）
models/                 # 本地模型权重（不入库）
third_party/
  llama.cpp/            # git submodule（v0.3.0 / c1d0e7a004015f23bc0233470b747b596f29b264）
  ffmpeg/               # gyan.dev 预编译 shared 构建（不入库）
  nlohmann/json.hpp     # vendored 单头文件
logs/events.jsonl       # 运行日志（不入库）
```

## 环境准备（Windows）

1. Visual Studio 2022 Build Tools（含 MSVC C++ 工具集）与 CMake ≥ 3.24。
2. 初始化 submodule：

   ```powershell
   git submodule update --init --recursive
   git -C third_party/llama.cpp checkout c1d0e7a004015f23bc0233470b747b596f29b264
   ```

3. FFmpeg：下载
   `https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.1-full_build-shared.7z`
   （SHA-256 `cb4d5e8db6a3353bffdb2100d3eb4b76733457fa443215e236f57c99f9ffdca4`），
   把其中的 `include/`、`lib/`、`bin/` 复制到 `third_party/ffmpeg/` 下。
4. 模型权重（放到 `models/`）：

   ```powershell
   curl.exe -L -o models\InternVL3-1B-Instruct-Q8_0.gguf `
     https://huggingface.co/ggml-org/InternVL3-1B-Instruct-GGUF/resolve/main/InternVL3-1B-Instruct-Q8_0.gguf
   curl.exe -L -o models\mmproj-InternVL3-1B-Instruct-Q8_0.gguf `
     https://huggingface.co/ggml-org/InternVL3-1B-Instruct-GGUF/resolve/main/mmproj-InternVL3-1B-Instruct-Q8_0.gguf
   curl.exe -L -o models\qwen2.5-1.5b-instruct-q4_k_m.gguf `
     https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf
   ```

   当前 Qwen Q4_K_M 文件 SHA-256：
   `6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e`。

依赖缺失时 CMake 会明确报错并列出缺失项与恢复步骤，不会静默回退。

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

首次构建会编译 llama.cpp/ggml/mtmd，需要较长时间。FFmpeg 与 llama 的运行 DLL
会在每次构建后自动复制到可执行文件目录。

## 测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

当前共 22 项。覆盖：FFmpeg 单向解码与 EOF flush、PTS pacing 墙钟时长、FrameFilter 时间采样与
帧差阈值、BoundedQueue 容量/drop_oldest/stop 唤醒/close 排空、结构化输出对非法
输入的安全失败、ToolRegistry 白名单与失败路径注入、Agent Loop 成功/失败/拒绝/
步数上限语义（MockModel），以及真实模型单图推理、Skill 行为切换、长时间运行
内存稳定性。

## 运行演示

用任意 MP4 模拟实时摄像头（按 PTS 节奏播放）：

```powershell
# 完整系统：视频 + 视觉模型 + 文本决策模型 + 唯一工具 + 远程面板
.\build\src\Release\edge_agent.exe `
    --video <你的视频.mp4> `
    --model models\InternVL3-1B-Instruct-Q8_0.gguf `
    --mmproj models\mmproj-InternVL3-1B-Instruct-Q8_0.gguf `
    --decision-model models\qwen2.5-1.5b-instruct-q4_k_m.gguf `
    --skill skills\door-camera.md `
    --web-port 8080 --unattended
```

主机输出 URL 后，在同一局域网的其它设备访问 `http://<主机局域网IP>:8080/`。
页面只显示 Agent 实际调用 `push_frame` 的帧；普通候选帧和 `final` 负例不会显示。

`door-camera.md` 当前演示边界是：同一帧内门口与明确地面物体可见时推送；
人员是否仍在画面内不影响门口纸箱的推送。
本机真实视频验收使用 Pexels 6170054（门口投递）：
`https://www.pexels.com/video/a-delivery-woman-leaving-a-box-at-a-door-6170054/`。
纯色/测试图 fixture 只验证解码、筛帧和队列，不作为模型能力证据。

本地还提供至少两分钟的拼接演示素材 `build/real-door-demo-2min.mp4`。它由上述
6170054 与 Pexels 6715786 两段真实视频交替组成，时长约 129 秒；第二段来源：
`https://www.pexels.com/video/woman-receiving-parcel-from-courier-6715786/`。
拼接会重复真实片段，适合展示长时间管线、网页刷新和有界历史，不用于统计模型准确率。

常用参数：`--no-pacing`（尽快解码，不做实时节奏）、`--no-vlm`（仅视频管线）、
`--max-seconds N`（只取前 N 秒）、`--sample-interval-ms 2000`、`--threshold 0.15`、
`--queue-capacity 6`、`--analysis-width 448`、`--log logs/events.jsonl`、
`--web-bind 0.0.0.0`、`--web-port 8080`、`--unattended`。

结束时打印统计（帧数 / 评估次数 / 候选数 / drop 数 / Agent 结果 / 平均 VLM 时延），
完整过程见 `logs/events.jsonl`（candidate、agent_result、tool_result、summary 等）。

## 性能参考（本地 CPU，RTX 无 GPU 参与，1080p 输入）

- 视频解码 + 筛帧：远快于实时；pacing 模式下严格按 PTS 节奏输出
- 候选比例：演示视频中 2161 帧 → 19 个候选（约 0.9%）
- VLM 平均时延：测试 fixture 约 6–13 s/候选；4K 竖屏真实视频约 38–39 s/候选
- 内存：视频管线工作集全程平稳（约 11–16 MB 波动），队列满时淘汰旧候选

## 已知限制

- 视觉质量受 InternVL3-1B 能力限制；文本路由使用短 Skill 和严格二分类降低小模型偏差
- 真实正例中视觉模型曾把普通场景的 `hazard` 判断为 visible；当前规则不使用该字段，
  但页面会如实显示模型输出。本项目证明本地管线与工具路由，不证明安防级识别准确率
- 当前唯一副作用是把关键帧推送到进程内只读 Web 面板，不发送外部通知
- FFmpeg 使用 gyan.dev 的 GPL v3 构建，仅限本地使用不分发；
  未来分发需重新决策许可（见《依赖接入决策.md》）
