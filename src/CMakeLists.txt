# 核心库：视频源、筛帧、队列、模型、Agent 等（随 Phase 逐步扩充）
add_library(edge_core STATIC
    video/ffmpeg_video_source.cpp
    video/yuv_to_rgb.cpp
    filter/frame_filter.cpp
    model/llama_vlm.cpp
    skill/skill_loader.cpp
    agent/structured_output.cpp
    agent/builtin_tools.cpp
    agent/agent.cpp
    app/pipeline.cpp)
target_include_directories(edge_core PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd")
target_link_libraries(edge_core PUBLIC
    ffmpeg::headers ffmpeg::avformat ffmpeg::avcodec ffmpeg::swscale ffmpeg::avutil
    nlohmann_json llama mtmd)

# edge_agent 主程序
add_executable(edge_agent main.cpp)
target_include_directories(edge_agent PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd")
target_link_libraries(edge_agent PRIVATE edge_core llama mtmd)
edge_copy_runtime_dlls(edge_agent)
