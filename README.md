# edge-agent：Linux 多模态边缘 Agent Runtime

C++20 本地视频 Agent。FFmpeg 按 PTS 单向解码，筛帧模块使用降采样 Y 平面；候选帧进入有界队列后，InternVL3-1B-Instruct 负责视觉事实，Qwen2.5-1.5B-Instruct 负责 Skill/工具决策。Agent 只允许 C++ `ToolRegistry` 白名单中的 `push_frame(summary)`。

该仓库当前没有模型权重、真实演示视频或运行日志。CI 不下载模型、不伪造能力演示，也不启动模型推理；运行需要操作者另行提供真实来源可追溯且有使用权的 GGUF、mmproj 和输入视频。

## 模块与交付

模块边界和产物见 [`docs/MODULES.md`](docs/MODULES.md)。CMake 将运行时拆为七个静态库：`edge_video`、`edge_filter`、`edge_model`、`edge_skill`、`edge_agent_core`、`edge_dashboard`、`edge_pipeline`，并生成 Linux `edge_agent` 可执行文件。`edge_core` 是供现有测试和下游代码使用的兼容聚合目标。

Jenkins 在 Ubuntu 24.04 x86_64 Docker 节点完成依赖初始化、边界检查、CMake 构建、CTest、二进制模块归档和隔离运行镜像构建。流水线不部署或启动推理服务。

二进制归档命名为 `cc-agent-cpp-<版本>-linux-x86_64.tar.gz`，附 SHA-256 与 `manifest-linux-x86_64.json`。归档含主程序、七个项目模块静态库、llama.cpp/ggml 静态库、头文件、Skill、模块文档及第三方许可证；明确不含模型权重和视频 fixture。运行时需要 Ubuntu 24.04 对应的 FFmpeg shared libraries 与 `libgomp1`。

## Linux Jenkins 环境

Jenkins 使用标签 `media-workspace-agent`，需要 Linux x86_64、Docker CLI/daemon、`sudo docker` 权限，以及能访问 GitHub 拉取锁定 llama.cpp submodule 的网络。构建限制为一个并行 job，以控制服务器内存。

流水线按顺序执行：

1. `git submodule update --init --recursive`，并验证 `third_party/llama.cpp` 的 commit 为 `c1d0e7a004015f23bc0233470b747b596f29b264`。
2. 检查模块边界。
3. 在 `Dockerfile.linux` 的 Ubuntu 24.04 build stage 内运行 CMake、完整 CTest 和 Linux 二进制打包。
4. 从 build image 提取版本化 `.tar.gz` 二进制交付物，核对 SHA-256 并归档。
5. 构建最终 runtime image，检查 `edge_agent --help`，并启动默认等待配置模式，核验 `/api/state` 是空事件列表且明确显示未运行推理。

默认分支与该 Jenkins agent 的 Docker 权限由服务器 job 配置提供。Jenkins 参数 `DeployDemo` 默认 `false`；只有显式启用且前面的构建与 smoke checks 成功后，才会调用 `scripts/deploy-demo-linux.sh` 发布等待配置页面。部署 smoke 仅映射服务器回环端口，不挂载或拉取模型/视频。

## Docker 环境隔离

`Dockerfile.linux` 使用多阶段构建：Ubuntu 24.04 build stage 安装 CMake/Ninja、FFmpeg 开发包和编译依赖；runtime stage 仅安装共享运行库，使用非 root 的 `edge` 用户。镜像默认运行 `--web-idle`：立即提供真实的空面板，显示“等待配置”，不读取视频、不加载模型、不产生帧或推理结果。进程存活 healthcheck 与 HTTP `/healthz` 只表示服务存活，不代表已加载模型或开始分析。

只需将宿主端口映射到容器 8080 即可即时访问该状态页：

```bash
sudo docker run -d --name cc-agent-cpp-demo -p 127.0.0.1:18103:8080 cc-agent-cpp:<tag>
```

服务器反向代理可访问 `http://127.0.0.1:18103/`。Jenkins 启用 `DeployDemo` 时，脚本在该回环端口发布等待配置页，验证 `/healthz` 和 `/api/state`，失败会移除候选容器并恢复旧容器。部署使用只读根目录、非 root、丢弃所有 Linux capabilities、禁止提权、256 MiB 内存、0.5 CPU 与 64 PID 限额，且不挂载模型或视频。开始分析仍需另外提供真实视频、模型权重与 Skill；流水线不下载或伪造这些素材。

模型和视频必须从外部只读挂载。以下命令是路径模板，目录和文件须由部署者准备；仓库及服务器当前没有这些素材：

```bash
sudo docker run --rm --read-only --tmpfs /tmp --tmpfs /workspace/logs \
  --mount type=bind,src=/srv/cc-agent-cpp/models,dst=/models,readonly \
  --mount type=bind,src=/srv/cc-agent-cpp/input/source.mp4,dst=/input/source.mp4,readonly \
  -p 127.0.0.1:18103:8080 cc-agent-cpp:<tag> \
  --video /input/source.mp4 \
  --model /models/InternVL3-1B-Instruct-Q8_0.gguf \
  --mmproj /models/mmproj-InternVL3-1B-Instruct-Q8_0.gguf \
  --decision-model /models/qwen2.5-1.5b-instruct-q4_k_m.gguf \
  --skill /opt/edge-agent/skills/door-camera.md \
  --log /workspace/logs/events.jsonl \
  --web-port 8080 --web-bind 0.0.0.0 --unattended
```

Docker 将面板仅绑定到宿主机回环地址 `127.0.0.1:18103`，供服务器反向代理接入。启用只读根文件系统时，日志目录仍需单独挂载为可写 volume；上例用临时目录说明运行方式，持久运行应改为受控的宿主机日志目录。

## 本地目录与依赖

- `third_party/llama.cpp` 是 Git submodule，固定 v0.3.0 commit；Jenkins 在 Linux 服务器拉取，不把第三方源码重复提交到主仓库。
- Linux FFmpeg 开发依赖使用 Ubuntu 24.04 的 `pkg-config` 包：`libavformat-dev`、`libavcodec-dev`、`libswscale-dev`、`libavutil-dev`；Docker build image 已声明这些依赖。
- `third_party/ffmpeg/` 是现存 Windows 开发流程的 gyan.dev 依赖，与 Linux Docker build 无关。
- `models/`、大体积 MP4、日志、`build/` 和 `dist/` 均不入 Git。

缺失依赖时 CMake 应明确终止并给出依赖版本、预期路径和恢复步骤；不得静默回退到其他供应方。

## 运行约束

- 视频按 PTS 单向读取；不 seek、不二遍扫描、不缓存全视频，视频路径内存为 O(1)。
- Video Worker 不等待 VLM；容量 4–8 的 `BoundedQueue` 满时淘汰最旧候选。
- 只有模型成功执行 `push_frame` 的关键帧会进入只读面板。
- Qwen 只接受精确 `PUSH` / `FINAL` 决策，Agent Loop 最多三次生成；无后续预算时返回 `STEP_LIMIT_REACHED`。
- 当前实现不承诺安防级视觉识别准确率。

## 许可说明

llama.cpp license 随二进制模块包一并交付；nlohmann/json 单头文件保留其源文件许可证声明。Linux runtime 使用 Ubuntu 24.04 的 FFmpeg 动态库，不把这些系统库复制进 TAR；容器镜像包含相同发行版运行包。任何对外再分发前，需按实际 Ubuntu 包、FFmpeg 构建和组合方式核对适用许可。

Windows FFmpeg 决策和 SHA-256 记录见 [`依赖接入决策.md`](依赖接入决策.md)。
