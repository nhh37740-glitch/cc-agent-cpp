# Module boundaries

`src/CMakeLists.txt` builds the runtime as small static libraries with an
explicit dependency direction. `edge_core` remains an interface target for
the existing integration tests and downstream consumers.

| Module | Owns | Depends on |
| --- | --- | --- |
| `edge_video` | FFmpeg input, frame representation, pacing, pixel conversion | FFmpeg imports |
| `edge_filter` | Candidate frame selection | `edge_video` |
| `edge_model` | llama.cpp adapters and model decision interfaces | llama.cpp, JSON |
| `edge_skill` | Skill file loading | standard library |
| `edge_agent_core` | Structured output, tool registry, agent loop | `edge_model`, `edge_skill` |
| `edge_dashboard` | Read-only status dashboard | `edge_agent_core`, `edge_video`, Winsock |
| `edge_pipeline` | Application orchestration | the modules above |

The dependency arrow points toward the lower level. Modules expose headers from
`include/`; implementation files stay in their owning `src/` directory. The
single `edge_agent.exe` remains the deployable unit because the Windows build
uses one process and the dashboard shares in-process state.

The current project is Windows-only: CMake deliberately rejects non-Windows
hosts, and its checked dependency set uses MSVC import libraries and Windows
DLLs. The Linux demonstration host therefore cannot build or run this image.
The Windows Jenkins lane and runtime Dockerfile are provided for a Jenkins
Windows agent with Windows-container support; they do not claim a Linux build.
