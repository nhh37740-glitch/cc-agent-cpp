# 自动化测试。每个 Phase 对应的测试在此注册。
# deps_probe：验证 FFmpeg 与 llama.cpp/mtmd 依赖真实可用（Phase 1 验收）。
add_executable(deps_probe deps_probe.cpp)
target_include_directories(deps_probe PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd")
target_link_libraries(deps_probe PRIVATE
    nlohmann_json ffmpeg::headers ffmpeg::avformat ffmpeg::avcodec ffmpeg::swscale ffmpeg::avutil llama mtmd)
edge_copy_runtime_dlls(deps_probe)
add_test(NAME deps_probe COMMAND deps_probe)
