# 第三方组件与许可随附

## llama.cpp

- 上游：https://github.com/ggml-org/llama.cpp
- 固定版本：v0.3.0，commit `c1d0e7a004015f23bc0233470b747b596f29b264`
- 许可证：MIT；发布归档中的 `third-party-licenses/llama.cpp/LICENSE` 从已锁定 submodule 复制。

## nlohmann/json

仓库 vendored 的 `third_party/nlohmann/json.hpp` 文件头记载 SPDX MIT 和 `Copyright (c) 2013-2023 Niels Lohmann`。以下 MIT 许可随 TAR 与 runtime image 交付：

```text
MIT License

Copyright (c) 2013-2023 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## FFmpeg

Linux 容器使用 Ubuntu 24.04 软件包提供的动态库，不把 `.so` 文件复制进独立 TAR。容器镜像包含这些系统运行包；再分发容器或组合二进制前，需根据实际 apt 包构建配置核对许可。Windows gyan.dev 构建的许可决策另见 `依赖接入决策.md`。
