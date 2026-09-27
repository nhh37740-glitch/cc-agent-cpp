# 模块边界与交付格式

`src/CMakeLists.txt` 把运行时拆成静态模块，按显式依赖方向连接。`edge_core` 保留为现有集成测试和下游调用的接口聚合目标。

| 模块 | 职责 | 依赖 |
| --- | --- | --- |
| `edge_video` | FFmpeg 输入、帧结构、PTS pacing、像素转换 | FFmpeg 导入目标 |
| `edge_filter` | 候选帧筛选 | `edge_video` |
| `edge_model` | llama.cpp/mtmd 适配器及决策模型接口 | llama.cpp、JSON |
| `edge_skill` | Skill 文件加载 | C++ 标准库 |
| `edge_agent_core` | 结构化输出、工具注册、Agent Loop | `edge_model`、`edge_skill` |
| `edge_dashboard` | 只读面板、健康接口 | `edge_agent_core`、`edge_video` |
| `edge_pipeline` | 应用编排、Worker 生命周期 | 上述模块 |

Linux CI 由 `Dockerfile.linux` 在 Ubuntu 24.04 x86_64 中构建，限制为单 job 并行。发布 TAR 含上述七个 `.a` 模块、`edge_agent`、锁定 llama.cpp/ggml 库、公共头文件、Skill、模块文档、依赖 license、`manifest.json` 与 `SHA256SUMS`。

模型权重与真实视频不是代码模块，不进入 Docker build context、runtime 镜像或发布 TAR。CMake 测试 fixture 在隔离 build stage 生成，CTest 完成后不进入 runtime 镜像或发布 TAR。由于当前仓库没有 GGUF 权重，依赖真实模型的测试不会注册；CI 不把它们报告成已通过。

`edge_agent` 作为主程序入口运行单进程管线。容器 healthcheck 仅检测主进程存活；若运行参数开启 Web 面板，`/healthz` 返回面板服务状态。
