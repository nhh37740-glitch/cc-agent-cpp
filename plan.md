# AGENTS.md 编写计划

状态：已执行；依赖预检和盲读复验通过。Phase 1 文档门禁已解除；实际开工仍须用户明确指令。本文件保留作为决策与处置记录；对上游 review 的正式回应在 `来自codex的review意见.md` 各条目之后。

## 1. 调查结论（2026-08-26）

- 项目目录当前只有三个文件：
  - `cpp本地小模型demo.md`：唯一的需求与设计来源（约 1800 行）。
  - `来自codex的review意见.md`：上游 Codex agent 的 review 通道，当前为空。
  - 根目录的大体积 MP4（约 4 GB）：当前测试视频输入。
- 没有代码、CMake、README、CI、git 仓库，也没有任何既有指令文件。
- 主要风险：未来会话在搭建 Phase 1 骨架时偏离计划文档中已经确定的约束。

以上内容为 2026-08-26 执行前快照；「来自codex的review意见.md 为空」「没有代码」等描述仅反映当时状态，不代表现状。

## 2. 用户决策

| 决策点 | 结论 |
| --- | --- |
| 源码接入方式（2026-08-26 会话内交互确认，取代本表早期"FetchContent 或 submodule"表述） | git submodule：llama.cpp 以 submodule 引入并锁定 commit |
| FFmpeg Windows 库来源（2026-08-26 会话内交互确认） | 不用 vcpkg；采用 FFmpeg 官网链接的 gyan.dev 第三方 Windows 构建，artifact 为 `ffmpeg-9.0.1-full_build-shared.7z`；该 artifact 整体为 GPL v3 构建（非 LGPL）；固定 URL、SHA-256、归档验证和运行期策略见《依赖接入决策.md》 |
| llama.cpp 锁定版本（2026-08-26 会话内交互确认） | v0.3.0（commit c1d0e7a004015f23bc0233470b747b596f29b264）；tag→peeled commit 映射与该 commit 的 mtmd 兼容性已于 2026-08-26 预检通过，证据见《依赖接入决策.md》第 5 节 |
| FFmpeg 许可历史更正 | 早期文档曾误记为 LGPL；该旧值已废弃，不构成当前决策。当前唯一有效结论为 GPL v3 构建，详见《依赖接入决策.md》 |
| 项目定位与文档取舍（2026-08-26 用户确认） | 本项目用于简历展示，优先完成可运行、可演示、可验证的 MVP；纯文档格式或审计完整性问题不阻塞 Phase 1，只有未解决的技术 P0/P1 才阻塞实现 |
| 注释与新文档语言 | 中文 |
| 上游 review 的"写入" | 在同一文件内、对应 review 条目后面追加回应 |
| review 读写时机 | 每个任务/Phase 开始前读取，完成后写回 |

## 3. 历史草案（已废弃，不得执行；最终版本见根目录 `AGENTS.md`）

````markdown
# AGENTS.md

## 唯一事实来源

- 全部需求、架构与验收标准在 `cpp本地小模型demo.md`。写代码前必须先读它。
- 当前仓库只有这份计划文档，没有代码。开发从 Phase 1（CMake 骨架）+ Phase 2（FFmpegVideoSource）开始。
- 根目录的大体积 MP4（约 4 GB）是当前测试视频输入；不要移动或删除。

## 上游 review 协作（强制）

- `来自codex的review意见.md` 是上游 agent（Codex）写入 review 的通道。
- 每个任务/Phase 开始前先读取该文件；存在未处理 review 时，必须先按意见调整方案或代码。
- 处理完成后，在同一文件内、对应 review 条目后面追加回应（已完成 / 有分歧及理由），再继续其他工作。

## 技术栈（不要偏离）

- C++20 + CMake。视频层只用 FFmpeg（libavformat/libavcodec/libswscale），禁止引入 OpenCV。
- 模型运行时是 llama.cpp 的 multimodal/mtmd 接口，模型为 InternVL3-1B-Instruct GGUF。
- FFmpeg 与 llama.cpp 通过 CMake FetchContent 或 git submodule 引入，不用 vcpkg。
- 并发用 `std::jthread` + `std::stop_token`，两个 Worker：Video 与 VLM/Agent。

## 不可违反的架构约束

- 本地 MP4 是模拟实时摄像头：按 PTS 节奏单向读取（realtime_pacing）。禁止 seek、二遍扫描、整段视频缓存。
- Video Worker 绝不等待 VLM；两者通过 `BoundedQueue<CandidateFrame>`（capacity 4~8，满时 drop_oldest）解耦。视频管线内存必须 O(1)。
- 筛帧只用降采样后的 Y plane（约 160×90）；帧被选为 Candidate 之后才做 YUV→RGB 转换。
- Agent Loop 最多 3 步。所有 ToolResult——成功、失败、非法调用被拒绝——都必须回填进模型上下文。
- 工具白名单由 C++ ToolRegistry 强制执行；Skill 文件声明的权限不可信。第一版只有 4 个工具：notify、save_event、get_time、speak。
- 模型输出必须是结构化 JSON（`tool_call` / `final`），不做自由文本工具调用的猜测解析。

## 代码风格

- struct + 普通函数优先；class 只管理状态和生命周期；不建继承体系（仅 VideoSource / Model 接口除外）。
- 日志用 JSONL（logs/events.jsonl），不要只打印自然语言。
- 注释和新文档用中文。解释时禁止比喻和类比，直接陈述技术事实。

## 构建

- 每个 Phase 的基础验证：
  ```bash
  cmake -S . -B build
  cmake --build build
  ```
- 每个 Phase 在计划文档里有独立验收标准，结束前逐条核对。
````

## 4. 执行记录

2026-08-26 实际创建：根目录 `AGENTS.md`（吸收 R-001～R-007 及第三轮复审修订后的版本）与本文件。「按第 3 节草案创建」的原定方案已被首轮 review 和第三轮复审的修订取代。

## 5. R-001～R-007 处置摘要（正式回应见 `来自codex的review意见.md` 对应条目后）

- R-001 [P1] 初始状态描述会过期：已接受。AGENTS.md 改为「检查现状确定 Phase，不据历史状态推断」，并要求只实现用户当前要求的 Phase。
- R-002 [P1] max_steps 与 ToolResult 回填冲突：已接受。采用「最多 3 次模型生成」定义；无后续生成机会时不执行新工具调用，返回 `STEP_LIMIT_REACHED`。
- R-003 [P2] 事实来源优先级：已接受。AGENTS.md 新增文档优先级节：用户指令 > 计划文档（需求/架构/验收）；本文件保存跨任务约束与已接受修订；冲突时指出并询问用户。
- R-004 [P2] review 通道协议：已接受。原始 review 只读；条目编号 R-001 起；回应一次、四种状态；回应须附证据。
- R-005 [P2] 依赖策略不可复现：部分接受。「不用 vcpkg」保留；完整依赖决策（接入方式、固定 tag/commit、Windows FFmpeg 构建方式、无网络行为、运行库路径、许可证）定为 Phase 1 的前置任务，须经用户确认后落盘，具体版本选择待用户参与。
- R-006 [P2] 大文件规则：已接受。AGENTS.md 新增测试资产规则节（不入库、gitignore 先行、路径经 CLI/配置传入、小型 fixture）。
- R-007 [P2] 构建命令不等于验收：已接受。每个 Phase 必须运行对应验收测试并记录命令与结果；重点自动化测试清单写入 AGENTS.md。

## 6. 盲读验证记录（R-012）

- 输入范围（明确文件名）：`AGENTS.md`、`plan.md`、`来自codex的review意见.md`、`cpp本地小模型demo.md`，共四个文件。
- 执行者与时间：2026-08-26，由一个未接触本对话的隔离 agent（opencode explore 子代理，会话 ses_fc30a9f7dffe0IZ5wGANil5YDJ）仅阅读上述文件后作答。
- 验证问题：①如何判断当前进度与应做 Phase；②依赖如何接入、Phase 1 第一步做什么；③Agent Loop 步数精确语义及第 3 次生成输出 tool_call 的处理；④上游 review 处理协议（原始内容/回应格式/状态/阻塞规则）。
- 结果：四问均有明文依据可答。验证 agent 正确回答「Phase 1 当前被 R-010（依赖决策未经用户确认）阻塞」，且未自行选择依赖方案——符合预期。
- 发现的歧义及处置：
  1. 依赖候选清单与用户原始决定不一致 → 已由用户裁决（git submodule + FFmpeg 官网链接的 gyan.dev 第三方 Windows 构建，artifact 为 ffmpeg-9.0.1-full_build-shared.7z，GPL v3 构建（非 LGPL）），AGENTS.md 已更新；
  2. Phase 1 被阻塞 → 属实，解除条件为前置版本决策经用户确认落盘；
  3. plan.md 状态过期、旧草案误导 → 本文件已按 R-009 修订；
  4. 计划文档第 17 节旧伪代码与新步数语义冲突 → 按 AGENTS.md 文档优先级规则消解，原文不改；
  5. R-008/R-011/R-012 未闭环 → 本次一并处理；
  6. STEP_LIMIT_REACHED 返回类型待实现期定义 → 已按 R-013 写入 AGENTS.md 约束，具体类型留待 Phase 9；
  7. 本节原写"三个 md 文件" → 已改为明确文件名列表。
- 结论：（2026-08-26 首验）验证通过；遗留项均已转入对应处置流程，无未记录歧义。注意：本次首验基于修订前快照，其"Phase 1 被 R-010 阻塞"的表述已被第 2 节最新用户决策与 R-015 修订取代；对当前最终文件的复验见下方第 7 节。

## 7. 盲读复验记录（R-016）

- 输入范围：`AGENTS.md`、`plan.md`、`来自codex的review意见.md`、`依赖接入决策.md`、`cpp本地小模型demo.md`，共五个文件。
- 执行者与时间：2026-08-26，由未接触对话的隔离 explore 子代理执行（会话 ses_fc2f6f5a0ffeXdPW3kWJIkXzL5）。
- 结果要点：
  1. 来源方案已固定（llama.cpp submodule v0.3.0 / commit c1d0e7a…；FFmpeg gyan.dev ffmpeg-9.0.1-full_build-shared.7z），后续 agent 不得重新选择——符合要求第 1 条；
  2. tag/commit/artifact/URL/SHA-256 全部有据可查，AGENTS.md 与决策文档无数值冲突；复验 agent 未自行补值——符合要求第 3 条；
  3. 许可性质被正确识别为 GPL v3 构建而非 LGPL，历史 LGPL 残留均已被文档自身显式标注并消解；
  4. 步数语义（最多 3 次模型生成）与 STEP_LIMIT_REACHED 非 ToolResult 的表述各文件一致；
  5. 复验发现的缺口一：P1 列名阻塞条件在文本上已全部满足，但缺少显式「Phase 1 放行」声明——由本节补充该声明；
  6. 缺口二：R-016 自身未闭环、review 状态总表缺 R-016/R-017 两行——由本次记录及 review 文件同步补齐。
- 结论：五份文档核心事实一致、无未消解矛盾。**Phase 1 的全部列名阻塞条件均已满足：《依赖接入决策.md》经用户确认并落盘，盲读复验通过。自本记录起 Phase 1 解除阻塞，可以开工**；实际开工仍须用户指令，并遵守「只实现用户当前要求的 Phase」。
