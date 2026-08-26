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

# ---------------------------------------------------------------------------
# 视频测试 fixture：用 third_party 的 ffmpeg.exe 生成小型测试视频（不入库）
# ---------------------------------------------------------------------------
set(FFMPEG_EXE "${FFMPEG_ROOT}/bin/ffmpeg.exe")
set(FIXTURE_DIR "${CMAKE_BINARY_DIR}/test-videos")
file(MAKE_DIRECTORY "${FIXTURE_DIR}")
set(FIXTURE_6S "${FIXTURE_DIR}/testsrc_6s.mp4")   # 动态画面：用于通用解码与筛帧
set(FIXTURE_10S "${FIXTURE_DIR}/testsrc_10s.mp4") # 动态画面：用于 pacing 时长验收

add_test(NAME make_fixture_6s
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i testsrc=size=320x240:rate=30:duration=6
        -pix_fmt yuv420p "${FIXTURE_6S}")
add_test(NAME make_fixture_10s
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i testsrc=size=320x240:rate=30:duration=10
        -pix_fmt yuv420p "${FIXTURE_10S}")
set_tests_properties(make_fixture_6s make_fixture_10s PROPERTIES FIXTURES_SETUP VideoFixtures)

# Phase 2：VideoSource 单向解码 + EOF flush
add_executable(video_source_test video_source_test.cpp)
target_link_libraries(video_source_test PRIVATE edge_core)
edge_copy_runtime_dlls(video_source_test)
add_test(NAME video_source_test COMMAND video_source_test "${FIXTURE_6S}")
set_tests_properties(video_source_test PROPERTIES FIXTURES_REQUIRED VideoFixtures)

# Phase 3：realtime pacing
add_executable(pacing_test pacing_test.cpp)
target_link_libraries(pacing_test PRIVATE edge_core)
edge_copy_runtime_dlls(pacing_test)
add_test(NAME pacing_test COMMAND pacing_test "${FIXTURE_10S}")
set_tests_properties(pacing_test PROPERTIES FIXTURES_REQUIRED VideoFixtures TIMEOUT 120)
