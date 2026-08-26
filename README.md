# edge-agent：本地多模态边缘 Agent Runtime（MVP）

使用 C++20、FFmpeg 与 llama.cpp（mtmd）构建的本地多模态 Agent：
以持续视频流为输入，通过低成本事件筛选和有界队列控制资源，
由 InternVL3-1B-Instruct VLM 按 Skill 进行理解与工具决策，
并通过受控 Agent Loop 完成工具执行与失败恢复。

## 架构

```
本地 MP4（模拟实时摄像头，按 PTS 单向读取，不 seek）
        ↓  Video Worker (jthread A)
FFmpeg 解码 → FrameFilter（时间采样 + Y 平面降采样帧差）
        ↓  候选帧确定后才转 RGB 并降采样
BoundedQueue<CandidateFrame>（容量 4~8，满时 drop_oldest）
        ↓  VLM Worker (jthread B)
llama.cpp mtmd + InternVL3-1B → Agent Loop（最多 3 次模型生成）
        ↓
ToolRegistry 白名单工具（notify / save_event / get_time / speak）
        ↓
ToolResult（成功/失败/拒绝）回填模型上下文
```

核心约束：

- 视频单向流式读取，禁止 seek / 二遍扫描 / 整段缓存；管线内存 O(1)
- Video Worker 绝不等待 VLM；队列满时 drop_oldest（摄像头语义）
- Skill 用 Markdown 定义，更换任务无需重编译；但权限只认 C++ 白名单
- 模型输出必须是结构化 JSON（`tool_call` / `final`），非法输出安全失败并回填错误
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
   ```

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

覆盖：FFmpeg 单向解码与 EOF flush、PTS pacing 墙钟时长、FrameFilter 时间采样与
帧差阈值、BoundedQueue 容量/drop_oldest/stop 唤醒/close 排空、结构化输出对非法
输入的安全失败、ToolRegistry 白名单与失败路径注入、Agent Loop 成功/失败/拒绝/
步数上限语义（MockModel），以及真实模型单图推理、Skill 行为切换、长时间运行
内存稳定性。

## 运行演示

用任意 MP4 模拟实时摄像头（按 PTS 节奏播放）：

```powershell
# 完整系统：视频 + VLM + Agent + 工具
.\build\src\Release\edge_agent.exe `
    --video <你的视频.mp4> `
    --model models\InternVL3-1B-Instruct-Q8_0.gguf `
    --mmproj models\mmproj-InternVL3-1B-Instruct-Q8_0.gguf `
    --skill skills\door-camera.md `
    --max-seconds 90
```

常用参数：`--no-pacing`（尽快解码，不做实时节奏）、`--no-vlm`（仅视频管线）、
`--max-seconds N`（只取前 N 秒）、`--sample-interval-ms 2000`、`--threshold 0.15`、
`--queue-capacity 6`、`--analysis-width 448`、`--log logs/events.jsonl`。

结束时打印统计（帧数 / 评估次数 / 候选数 / drop 数 / Agent 结果 / 平均 VLM 时延），
完整过程见 `logs/events.jsonl`（candidate、agent_result、tool_result、summary 等）。

## 性能参考（本地 CPU，RTX 无 GPU 参与，1080p 输入）

- 视频解码 + 筛帧：远快于实时；pacing 模式下严格按 PTS 节奏输出
- 候选比例：演示视频中 2161 帧 → 19 个候选（约 0.9%）
- VLM 平均时延：约 6–13 s/候选（纯 CPU，含视觉编码与 ≤3 轮生成）
- 内存：视频管线工作集全程平稳（约 11–16 MB 波动），队列满时淘汰旧候选

## 已知限制

- ChatML 提示模板针对 InternVL3 系列；换其他模型需调整模板
- 小模型偶发输出不符合协议的 JSON；系统会回填错误让模型修正，
  若发生在最后一次生成则按步数上限终止
- `speak` 仅命令行占位，无 TTS；`save_event` 写本地 JSONL
- FFmpeg 使用 gyan.dev 的 GPL v3 构建，仅限本地使用不分发；
  未来分发需重新决策许可（见《依赖接入决策.md》）
