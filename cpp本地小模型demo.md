# 本地多模态边缘 Agent：完整开发计划

## 0. Review 结论

前面的方向已经基本收敛，可以正式进入实现。

最初的目标是：**C++ + llama.cpp + 小型 VLM + Skill + Agent Loop + 少量工具**，并限制 Agent 步数、上下文和资源使用。
Skill 用 Markdown/YAML 把“什么值得通知”从 C++ 代码中分离出来，这个方向继续保留。

经过后续讨论，需要对原计划做四个关键修正：

1. **视频层从一开始就使用 FFmpeg**，不走 OpenCV。
2. 本地视频不是“离线视频分析”，而是**模拟实时摄像头的单向视频源**，禁止依赖 seek、二遍扫描。
3. FFmpeg 解码/筛帧与 VLM 分析必须**并行运行**，通过有界数据队列解耦。
4. Agent 的 ToolResult 无论成功还是失败，都必须**重新反馈给模型**，让模型决定继续、重试、换工具或结束。

最终目标不是做一个“视频问答 Demo”，而是做一个小型、可迁移的：

> **Local Multimodal Agent Runtime for Edge Devices**

---

# 1. 项目最终目标

## 1.1 核心目标

实现一个 C++20 本地程序，可以：

```text
本地 MP4（模拟摄像头）
        ↓
FFmpeg 持续解码
        ↓
低成本帧筛选
        ↓
候选帧队列
        ↓
本地约 1B VLM
        ↓
Skill + Agent Loop
        ↓
工具调用
        ↓
ToolResult
        ↓
重新反馈 VLM
```

整个系统必须满足：

```text
流式
并行
有界内存
本地运行
可迁移摄像头
工具受控
失败可恢复
Skill 可替换
```

---

# 2. 明确不做什么

第一版刻意不做：

```text
× 多 Agent
× 长期 Memory
× RAG
× 任意 Shell
× 任意文件访问
× 实时视频逐帧 VLM
× 视频完整缓存
× 云端模型
× 真正 Push Notification
× TTS 引擎
× 摄像头硬件接入
× RTSP
× 音频
```

这些全部属于后续阶段。

第一版首先证明：

> **小模型能不能在持续视频环境下，通过 Skill 做判断，并可靠调用工具。**

---

# 3. 最终技术栈

| 模块            | 选择                          |
| ------------- | --------------------------- |
| Language      | C++20                       |
| Build         | CMake                       |
| Video         | FFmpeg                      |
| 解码            | libavformat + libavcodec    |
| 图像转换          | libswscale                  |
| Model Runtime | llama.cpp                   |
| 多模态接口         | llama.cpp multimodal / mtmd |
| 当前 VLM        | InternVL3-1B-Instruct GGUF  |
| Agent         | 自研轻量 Agent Loop             |
| Skill         | Markdown                    |
| Tool Protocol | JSON                        |
| 并发            | `std::jthread`              |
| 停止控制          | `std::stop_token`           |
| 队列            | 有界线程安全队列                    |
| Log           | JSONL                       |

原则仍然是：

```text
struct + 普通函数优先
class 只管理状态和生命周期
不搞复杂继承体系
```

---

# 4. 最终系统架构

```text
                  ┌────────────────────┐
                  │     VideoSource    │
                  │ FFmpegFileSource   │
                  └─────────┬──────────┘
                            │
                         AVFrame
                            │
                            ▼
                  ┌────────────────────┐
                  │    FrameFilter     │
                  │                    │
                  │ 时间采样           │
                  │ ↓                  │
                  │ 低分辨率帧差       │
                  │ ↓                  │
                  │ scene score        │
                  └─────────┬──────────┘
                            │
                       Candidate
                            │
                            ▼
                ┌──────────────────────┐
                │ BoundedFrameQueue    │
                │ capacity = 4~8       │
                └──────────┬───────────┘
                           │
                           ▼
                  ┌────────────────────┐
                  │     VLM Worker     │
                  │                    │
                  │ llama.cpp          │
                  │ InternVL3-1B       │
                  └─────────┬──────────┘
                            │
                      Observation
                            │
                            ▼
                  ┌────────────────────┐
                  │       Agent        │
                  │                    │
                  │ Skill              │
                  │ Conversation       │
                  │ Agent Loop ≤ 3     │
                  └─────────┬──────────┘
                            │
                       ToolCall
                            │
                            ▼
                  ┌────────────────────┐
                  │   ToolRegistry     │
                  │ + Validator        │
                  └─────────┬──────────┘
                            │
          ┌─────────────────┼─────────────────┐
          ▼                 ▼                 ▼
       notify          save_event         get_time
          │                 │                 │
          └─────────────────┼─────────────────┘
                            ↓
                       ToolResult
                            │
                     success/error
                            │
                            └──────────→ Agent/VLM
```

---

# 5. 线程模型

第一版只需要两个核心 Worker。

## Thread A：Video Worker

负责：

```text
读取视频
→ 解码
→ 根据 PTS 模拟摄像头节奏
→ 低成本筛帧
→ 产生 CandidateFrame
→ push queue
```

它**绝不等待 VLM**。

---

## Thread B：VLM / Agent Worker

负责：

```text
queue.pop()
→ VLM 分析
→ Agent 判断
→ Tool Call
→ ToolResult
→ 回填模型
→ 下一轮
```

VLM 慢不会阻止 FFmpeg 继续取流。

---

# 6. 本地视频如何模拟摄像头

这是设计里非常重要的一点。

普通 FFmpeg 读取：

```text
10 分钟视频

可能几十秒就解码完
```

这并不能模拟摄像头。

因此需要提供：

```text
realtime_pacing = true
```

根据视频 PTS 控制帧产生速度。

例如视频：

```text
00:00.000
00:00.033
00:00.066
00:00.100
```

Video Worker 也按照这个节奏输出。

也就是说：

```text
视频输入速度 ≈ 摄像头真实产生速度
```

但：

> **VLM 不要求实时完成。**

这是两个不同概念。

我们模拟的是：

**实时数据流**

而不是要求：

**实时 AI 响应。**

---

# 7. FrameFilter：低成本判断层

这是控制计算量的关键。

绝不能：

```text
30 FPS
↓
30 张/s
↓
VLM
```

而应该：

```text
30 FPS
 ↓
时间门控
 ↓
低分辨率变化分析
 ↓
事件过滤
 ↓
少量 Candidate
```

---

## 第一层：时间采样

例如：

```text
每 2 秒检查一次
```

30 FPS 视频：

```text
60 帧
↓
只检查 1 帧
```

首先降低约 60 倍。

配置：

```yaml
frame_filter:
  sample_interval_ms: 2000
```

---

# 8. 第二层：低分辨率变化检测

不直接对 1920×1080 做分析。

转换成：

```text
160 × 90
```

甚至只使用亮度 Y channel。

比如：

```text
1920×1080 YUV
      ↓
取 Y Plane
      ↓
160×90
      ↓
与 previous 比较
```

计算：

```text
change_score
```

例如：

```text
0.02  基本静止
0.08  小变化
0.32  明显变化
0.71  大规模场景变化
```

达到阈值才进入队列。

---

# 9. 一个重要优化：不要每帧 RGB 转换

FFmpeg 解码出来通常本身就是 YUV。

因此：

```text
AVFrame
↓
直接使用 Y Plane 做筛选
```

只有判断：

```text
should_analyze == true
```

以后才：

```text
libswscale
↓
RGB
↓
VLM 输入
```

于是：

```text
绝大多数视频帧
```

根本不会发生昂贵的：

```text
YUV → RGB
```

转换。

这对以后真正跑边缘设备非常重要。

---

# 10. CandidateFrame

建议定义：

```cpp
struct CandidateFrame {
    int64_t pts;
    double timestamp;

    float change_score;

    int width;
    int height;

    std::vector<uint8_t> rgb;
};
```

注意：

**只在 Candidate 产生后保存 RGB。**

普通 AVFrame：

```text
分析完成
→ 立即复用/释放
```

---

# 11. 有界数据队列

允许队列，但绝不能：

```cpp
std::queue<CandidateFrame>
```

无限增长。

应该明确：

```text
capacity = 4~8
```

例如：

```cpp
BoundedQueue<CandidateFrame> queue(8);
```

---

# 12. Queue 满时怎么办

第一版采用最简单策略：

```text
drop oldest
```

例如：

```text
Queue

A
B
C
D
E
F
G
H

新候选 I 到达
↓
删除 A
↓
保留
B C D E F G H I
```

理由：

这是摄像头语义。

通常：

```text
现在发生什么
```

比：

```text
30 秒前发生什么
```

更重要。

---

## 后续可以升级

第二版：

```text
timestamp + change_score
```

共同决定淘汰。

例如队列满：

```text
旧 + score低
```

优先删除。

---

# 13. 内存目标

系统中最多保留：

```text
1 个 FFmpeg 当前 Decode Frame

1 个 Previous Small Frame
    约 160×90

4~8 个 CandidateFrame

1 个 VLM 正在分析的 Frame

模型权重
KV Cache
```

所以：

```text
视频 10 分钟
视频 10 小时
视频 10 天
```

视频长度不会造成内存持续增长。

目标是：

> **Video Pipeline 内存 O(1)。**

---

# 14. VLM Worker

当前暂定：

```text
InternVL3-1B-Instruct GGUF
```

通过 llama.cpp 运行。

VLM 每次输入：

```text
一张 CandidateFrame
+
当前 Skill
+
系统 Prompt
+
必要的短 Conversation
```

不是把完整历史视频输入模型。

---

# 15. VLM 的职责

VLM主要回答：

```text
这张画面发生了什么？
这件事情是否符合 Skill？
需要调用工具吗？
```

例如：

```text
画面：
门口出现纸箱

Skill：
新包裹出现需要通知

VLM：
notify(...)
```

---

# 16. Agent 输出协议

第一版不要依赖自由文本猜 Tool Call。

统一结构化 JSON。

例如：

```json
{
  "type": "tool_call",
  "name": "notify",
  "arguments": {
    "text": "门口出现了一个新包裹"
  }
}
```

如果不需要动作：

```json
{
  "type": "final",
  "content": "No action required."
}
```

---

# 17. Agent Loop

最大：

```text
3 steps
```

完整逻辑：

```text
VLM
 ↓
Tool Call?
 ├─ NO → Finish
 │
 └─ YES
      ↓
   Validate
      ↓
   Execute
      ↓
   ToolResult
      ↓
   回填 VLM
      ↓
   下一步
```

伪代码：

```cpp
for (int step = 0; step < max_steps_; ++step) {

    ModelResponse response =
        model.generate(messages);

    if (response.type == Final)
        return response;

    ToolResult result =
        tools.execute(response.tool_call);

    messages.push_back(
        make_tool_result_message(result)
    );
}
```

---

# 18. ToolResult 必须包含失败

这是我们现在确定的一个重要设计原则。

工具成功：

```json
{
  "type": "tool_result",
  "tool": "notify",
  "success": true,
  "data": {
    "message": "Notification printed"
  }
}
```

工具失败：

```json
{
  "type": "tool_result",
  "tool": "notify",
  "success": false,
  "error": {
    "code": "OUTPUT_ERROR",
    "message": "Failed to write to stdout"
  }
}
```

然后：

```text
ToolResult
↓
重新进入模型上下文
```

让模型知道：

> 刚刚的动作没有成功。

---

# 19. 非法工具调用也反馈模型

比如模型产生：

```json
{
  "name": "delete_file"
}
```

系统绝不能执行。

但可以返回：

```json
{
  "type": "tool_result",
  "tool": "delete_file",
  "success": false,
  "error": {
    "code": "TOOL_NOT_ALLOWED",
    "message": "Tool is not in the whitelist"
  }
}
```

于是模型可以修正行为。

---

# 20. Tool Registry

第一版只有四个工具。

## `notify`

现在明确：

```text
直接命令行打印
```

例如：

```text
[NOTIFY] 门口出现了一个新包裹
```

接口：

```cpp
ToolResult notify(const json& args);
```

---

## `save_event`

写入：

```text
logs/events.jsonl
```

例如：

```json
{
  "timestamp": 123.4,
  "event": "package_detected",
  "description": "A package appeared near the door"
}
```

---

## `get_time`

返回当前时间。

例如：

```json
{
  "success": true,
  "data": {
    "time": "2026-08-26T15:30:00+09:00"
  }
}
```

---

## `speak`

第一版不接真正 TTS。

可以暂时：

```text
[SPEAK] xxxx
```

以后再替换真正语音后端。

---

# 21. ToolRegistry 设计

不要设计：

```text
Tool
 ↓
BaseTool
 ↓
NotifyTool
 ↓
ConsoleNotifyTool
```

没有必要。

直接：

```cpp
using ToolFn =
    std::function<ToolResult(const json&)>;

std::unordered_map<std::string, ToolFn> tools;
```

例如：

```cpp
tools["notify"] = notify;
tools["save_event"] = save_event;
tools["get_time"] = get_time;
tools["speak"] = speak;
```

保持轻量。

---

# 22. Skill 系统

Skill 仍然是核心设计。

例如：

```text
skills/
├── door-camera.md
├── pet-watch.md
├── garage-watch.md
└── inspection.md
```

---

## door-camera.md

例如：

```markdown
# Door Camera

Only notify when:

1. A new package appears.
2. Someone stays near the door for a meaningful period.
3. Smoke, fire, falling, or destructive behavior is visible.
4. A user-specified person or object appears.

Do not notify for:

- ordinary pedestrians;
- unchanged scenes;
- insignificant movement.

Allowed tools:

- notify
- save_event
- speak
- get_time
```

这样：

```text
更换任务
```

不需要重新编译程序。

---

# 23. Skill 与 Tool 权限必须分离

Skill可以告诉模型：

```text
允许 notify
```

但真正权限不能相信 Skill。

真正权限仍然来自 C++：

```text
ToolWhitelist
```

因此：

```text
Skill：
“调用 delete_file”

         ↓

Agent：
产生 delete_file

         ↓

C++ Validator：
DENIED
```

模型永远没有最终控制权。

---

# 24. 模块划分

建议项目最终：

```text
edge-agent/
│
├── CMakeLists.txt
│
├── include/
│   ├── video/
│   │   ├── frame.hpp
│   │   ├── video_source.hpp
│   │   └── ffmpeg_video_source.hpp
│   │
│   ├── filter/
│   │   └── frame_filter.hpp
│   │
│   ├── queue/
│   │   └── bounded_queue.hpp
│   │
│   ├── model/
│   │   ├── model.hpp
│   │   └── llama_vlm.hpp
│   │
│   ├── agent/
│   │   ├── agent.hpp
│   │   ├── message.hpp
│   │   └── tool_call.hpp
│   │
│   ├── tools/
│   │   ├── tool_registry.hpp
│   │   └── tool_result.hpp
│   │
│   └── skill/
│       └── skill_loader.hpp
│
├── src/
│
├── skills/
│   └── door-camera.md
│
├── config/
│   └── default.yaml
│
├── models/
│
├── examples/
│   └── test.mp4
│
├── logs/
│
└── tests/
```

---

# 25. 核心接口

## VideoSource

```cpp
class VideoSource {
public:
    virtual bool read(Frame& frame) = 0;
    virtual ~VideoSource() = default;
};
```

现在：

```text
FFmpegFileSource
```

以后：

```text
FFmpegCameraSource
FFmpegRTSPSource
```

---

## FrameFilter

```cpp
struct FilterResult {
    bool should_analyze;
    float score;
};

class FrameFilter {
public:
    FilterResult analyze(const Frame& frame);
};
```

---

## Model

```cpp
class Model {
public:
    virtual ModelResponse generate(
        const std::vector<Message>& messages
    ) = 0;

    virtual ~Model() = default;
};
```

---

## Agent

```cpp
class Agent {
public:
    Agent(
        Model& model,
        ToolRegistry& tools
    );

    AgentResult run(
        const Observation& observation
    );
};
```

---

# 26. 配置文件

建议最终：

```yaml
video:
  path: "examples/test.mp4"
  realtime_pacing: true

frame_filter:
  sample_interval_ms: 2000
  width: 160
  height: 90
  change_threshold: 0.15

queue:
  capacity: 8
  overflow_policy: "drop_oldest"

model:
  path: "models/model.gguf"
  context_tokens: 2048

agent:
  max_steps: 3

skill:
  path: "skills/door-camera.md"

logging:
  path: "logs/events.jsonl"
```

---

# 27. 开发阶段

## Phase 1 — 项目骨架

### 目标

建立可编译 C++20 项目。

### 完成内容

```text
CMake
FFmpeg dependency
llama.cpp dependency
目录结构
基础日志
```

### 验收

```bash
cmake -S . -B build
cmake --build build
```

成功。

---

# 28. Phase 2 — FFmpeg VideoSource

### 目标

能够持续从：

```text
test.mp4
```

产生 Frame。

实现：

```text
avformat_open_input
avformat_find_stream_info
avcodec_find_decoder
avcodec_open2
av_read_frame
avcodec_send_packet
avcodec_receive_frame
```

必须正确处理：

```text
EOF
decoder flush
PTS
错误返回
资源释放
```

### 验收

打印：

```text
PTS=0.00
PTS=0.033
PTS=0.066
...
```

且内存稳定。

---

# 29. Phase 3 — 摄像头模拟

### 目标

本地 MP4 表现得像持续摄像头。

增加：

```text
realtime_pacing
```

### 验收

10 秒视频大约需要：

```text
10 秒
```

产生完，而不是瞬间解码完成。

---

# 30. Phase 4 — FrameFilter

### 目标

大量减少进入 VLM 的图片。

实现：

```text
时间采样
+
160×90低分辨率图
+
帧差
+
threshold
```

例如：

```text
300 帧

↓ FrameFilter

5 个 Candidate
```

### 验收

静止视频：

```text
Candidate 很少
```

明显场景变化：

```text
Candidate 被正确产生
```

---

# 31. Phase 5 — Bounded Queue

### 目标

FFmpeg 与 VLM 解耦。

实现：

```cpp
BoundedQueue<CandidateFrame>
```

支持：

```text
push
pop
stop
capacity
drop_oldest
```

必须线程安全。

### 验收

人工让 Consumer：

```text
sleep 10 秒
```

Video Worker仍持续运行。

Queue：

```text
永远 <= capacity
```

---

# 32. Phase 6 — VLM 单图推理

### 目标

先完全不加 Agent。

输入：

```text
CandidateFrame
```

得到：

```text
"There is a package near the door."
```

### 验收

连续几十张 Candidate：

```text
无 crash
无持续内存增长
```

---

# 33. Phase 7 — Skill

### 目标

加载：

```text
door-camera.md
```

组合：

```text
System Prompt
+
Skill
+
Image
```

输入模型。

验收：

换成：

```text
pet-watch.md
```

模型行为发生变化，而 C++ 不变。

---

# 34. Phase 8 — Tool Calling

### 目标

模型产生：

```json
{
  "type": "tool_call",
  "name": "notify",
  "arguments": {}
}
```

实现：

```text
JSON parse
Schema validation
Whitelist
ToolRegistry
```

### 验收

正确工具：

```text
执行
```

不存在工具：

```text
拒绝
```

非法 JSON：

```text
拒绝
```

---

# 35. Phase 9 — Agent Loop

### 目标

真正形成：

```text
Model
↓
Tool
↓
ToolResult
↓
Model
```

最大：

```text
3 steps
```

### 核心测试

故意制造：

```text
notify failed
```

模型必须收到：

```json
{
  "success": false
}
```

而不是误认为通知成功。

这是 MVP 的重要验收项。

---

# 36. Phase 10 — 完整并行系统

最终：

```text
Video Worker
       │
       ↓
FrameFilter
       │
       ↓
Bounded Queue
       │
       ↓
VLM Worker
       │
       ↓
Agent
       │
       ↓
Tools
```

同时运行。

重点观察：

```text
Queue Size
Frame Drop
VLM Latency
Tool Result
RAM
CPU
```

---

# 37. 日志体系

不要只打印自然语言。

JSONL：

```json
{"type":"candidate","pts":21.3,"score":0.42}
{"type":"vlm","text":"A package is visible"}
{"type":"tool_call","tool":"notify"}
{"type":"tool_result","success":true}
```

以后才方便分析：

```text
VLM每次耗时
候选帧比例
queue overflow次数
tool error率
通知次数
Agent平均steps
```

---

# 38. 最关键的性能指标

第一版只关注六个：

| 指标                 | 意义        |
| ------------------ | --------- |
| Peak RAM           | 能否迁移边缘设备  |
| VLM latency        | 模型速度      |
| Candidate rate     | 筛帧是否有效    |
| Queue occupancy    | VLM是否严重落后 |
| Dropped candidates | 系统压力      |
| Agent steps        | 小模型是否容易绕圈 |

---

# 39. MVP 最终验收标准

只有下面全部通过，才算第一版真正完成：

```text
✓ C++20 编译

✓ FFmpeg读取本地MP4

✓ 按PTS模拟摄像头

✓ 视频单向读取，不seek

✓ FrameFilter工作

✓ 不把所有视频帧保存到内存

✓ FFmpeg Worker与VLM Worker并行

✓ Candidate Queue有界

✓ Queue overflow不会导致内存增长

✓ InternVL3-1B本地运行

✓ CandidateFrame可以送入VLM

✓ Markdown Skill生效

✓ 模型可以产生Tool Call

✓ 4个Tool注册成功

✓ notify打印CLI

✓ Tool成功Result返回模型

✓ Tool失败Result也返回模型

✓ 非法Tool被拒绝

✓ Agent Loop最多3步

✓ 视频持续运行时内存基本稳定

✓ 有完整JSONL日志
```

---

# 40. 第二阶段：真正摄像头

到这里以后：

```text
FFmpegFileSource
```

换成：

```text
FFmpegCameraSource
```

后面的：

```text
FrameFilter
Queue
VLM
Skill
Agent
Tools
```

**全部保持不变。**

这就是为什么现在必须坚持摄像头语义设计。

---

# 41. 第三阶段：RTSP

再加入：

```text
FFmpegRTSPSource
```

处理：

```text
断线
重连
超时
网络抖动
H264/H265
硬件Decode
```

Agent 完全不用知道输入来自哪里。

---

# 42. 第四阶段：语音

加入另一条生产者：

```text
Mic
↓
VAD
↓
ASR
↓
Event Queue
```

最终：

```text
        Camera
           ↓
      FrameFilter
           │
           │
Mic → VAD → ASR
           │
           ↓
       Event Layer
           ↓
          VLM
           ↓
         Agent
```

这时候项目才真正开始成为：

> **Multimodal Edge Agent Runtime**

---

# 43. 最终项目原则

整个项目以后都围绕五条原则：

### ① 流式，而不是批处理

```text
数据来了 → 处理 → 丢弃
```

不存完整历史。

### ② 有界，而不是无限缓存

```text
Queue
Context
Agent Steps
Memory
```

全部必须有上限。

### ③ 小模型处理高价值信息

```text
便宜判断层
↓
过滤
↓
昂贵 VLM
```

### ④ Model 只有建议权

```text
Model
↓
Tool Request
↓
C++ Policy
↓
Execution
```

模型没有直接系统权限。

### ⑤ 一切执行结果反馈模型

```text
成功
失败
拒绝
参数错误
超时
```

全部成为 `ToolResult`。

---

## 最终一句话定义

这个项目现在可以正式定义为：

> **一个使用 C++20、FFmpeg 和 llama.cpp 构建的轻量本地多模态 Agent Runtime：以持续视频流为输入，通过低成本事件筛选和有界队列控制资源，再由约 1B VLM 按 Skill 进行理解和工具决策，并通过受控 Tool Loop 完成执行与失败恢复。**

这套架构已经足够稳定，可以从 **Phase 1 → Phase 2：项目骨架 + `FFmpegVideoSource`** 正式开始写代码。
