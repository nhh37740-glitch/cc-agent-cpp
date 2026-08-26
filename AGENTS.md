# AGENTS.md

## 任务开始前（强制）

1. 先读 `来自codex的review意见.md`：存在未处理条目时先处理；P0/P1 未处理项阻塞相关 Phase，P2/P3 不自动阻塞但必须记录处置结果。
2. 再检查现有源码、CMake 文件、测试和最近的 review 回应，确定当前实际进度。项目在 2026-08-26 初始状态下没有代码；不要据此推断当前状态。
3. 只实现用户当前要求的 Phase，不要因为初始计划从 Phase 1/2 开始就重复搭骨架或越级实现。

## 文档优先级

- 当前用户指令优先于所有仓库文档。
- `cpp本地小模型demo.md`：产品需求、架构与各阶段验收标准的主要来源。
- `AGENTS.md`：跨任务工程约束，含已被接受的后续修订；与计划文档早期文本不一致时，以本文件及已接受的 review 回应为准。
- 文档冲突且无法按上述规则判定时，不要自行取舍：指出冲突并询问用户。

## 上游 review 协作协议

- `来自codex的review意见.md` 中 Codex 写入的原始 review 内容只读，处理者不得改写或删除。
- 每条意见使用稳定 ID（R-001 起）和优先级；处理者只在对应条目后追加一次「回应」，状态取：已接受 / 部分接受 / 有分歧 / 待用户决定。
- 回应必须包含证据（修改的文件、命令、测试结果）；再次进入同一任务时不得重复追加同一回应。

## 技术栈

- C++20 + CMake。视频层只用 FFmpeg（libavformat/libavcodec/libswscale），禁止引入 OpenCV。
- 模型运行时是 llama.cpp 的 multimodal/mtmd 接口，模型为 InternVL3-1B-Instruct GGUF。
- 并发用 `std::jthread` + `std::stop_token`，两个 Worker：Video 与 VLM/Agent。

## 依赖接入

- 不用 vcpkg。接入方案已经用户确认并落盘于根目录《依赖接入决策.md》，不得由后续 agent 重新选择：
  - llama.cpp：git submodule，锁定 v0.3.0（commit c1d0e7a004015f23bc0233470b747b596f29b264），add_subdirectory 接入。
  - FFmpeg：gyan.dev 的 ffmpeg-9.0.1-full_build-shared.7z（URL 与 SHA-256 见决策文档），解压至 third_party/ffmpeg/，find_path/find_library 导入头文件与导入库。注意：该构建含 GPL 组件，整体为 GPL v3 构建，不是 LGPL；仅本地使用不分发则无义务，未来分发前须重新决策或许可合规。
- 配置阶段任何依赖缺失必须明确报错（缺失项、期望版本与位置、恢复步骤），禁止静默回退到另一供应方或源码构建。

## 不可违反的架构约束

- 本地 MP4 是模拟实时摄像头：按 PTS 节奏单向读取（realtime_pacing）。禁止 seek、二遍扫描、整段视频缓存。
- Video Worker 绝不等待 VLM；两者通过 `BoundedQueue<CandidateFrame>`（capacity 4~8，满时 drop_oldest）解耦。视频管线内存必须 O(1)。
- 筛帧只用降采样后的 Y plane（约 160×90）；帧被选为 Candidate 之后才做 YUV→RGB 转换。
- Agent Loop 最多进行 3 次模型生成：
  - 只有仍保留至少一次后续模型生成机会时才执行新的工具调用；否则返回 `STEP_LIMIT_REACHED`，记录并结束；
  - 每个已执行或已拒绝的工具调用产生的 ToolResult，必须在下一次模型生成前加入上下文；
  - `STEP_LIMIT_REACHED` 是 AgentResult 的终止状态，不是任何工具的 ToolResult；日志须记录最后一个未执行的 tool_call 及 reason=no_followup_generation_budget，不伪造 ToolResult。
- 工具白名单由 C++ ToolRegistry 强制执行；Skill 文件声明的权限不可信。第一版只有 4 个工具：notify、save_event、get_time、speak。
- 模型输出必须是结构化 JSON（`tool_call` / `final`），不做自由文本工具调用的猜测解析。

## 测试资产规则

- 根目录的大体积 MP4 仅作为本地测试输入：不得复制、重命名、删除或提交到版本控制。
- 初始化 git 时先用 `.gitignore` 排除视频、模型权重、构建产物和运行日志。
- 测试输入路径通过 CLI 参数或配置传入；禁止硬编码任何视频文件名。
- 自动化测试使用单独生成的小型 fixture；大文件只用于手工或端到端测试。

## 代码风格

- struct + 普通函数优先；class 只管理状态和生命周期；继承体系仅限 VideoSource / Model 这类接口。
- 日志用 JSONL（logs/events.jsonl），不要只打印自然语言。
- 注释和新文档用中文。解释时禁止比喻和类比，直接陈述技术事实。

## 构建与验收

- 每个 Phase 除编译通过外，必须运行该 Phase 对应的验收测试，并在 review 回应中记录命令与结果。
- 重点自动化测试规划：`BoundedQueue` 容量与 drop_oldest 顺序及 stop 唤醒；PTS pacing 墙钟时长容差与非单调时间戳策略；FFmpeg EOF flush 与资源释放；Agent Loop 成功/失败/拒绝/非法参数的 ToolResult 均可见，步数上限时返回 STEP_LIMIT_REACHED 且不伪造 ToolResult；结构化输出对非法 JSON、未知字段、缺失字段、类型错误安全失败；长时间运行内存不随视频长度增长。
- 计划文档各 Phase 有独立验收标准，结束前逐条核对。
