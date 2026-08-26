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
set(FIXTURE_STATIC "${FIXTURE_DIR}/black_6s.mp4") # 静止画面：用于筛帧负例

# 动态 fixture：前 3 秒纯红、后 3 秒彩条，构成明确的场景切换
add_test(NAME make_fixture_6s
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i "color=c=red:size=320x240:rate=30:duration=3"
        -f lavfi -i "smptebars=size=320x240:rate=30:duration=3"
        -filter_complex "[0:v][1:v]concat=n=2:v=1:a=0[out]" -map "[out]"
        -pix_fmt yuv420p "${FIXTURE_6S}")
add_test(NAME make_fixture_10s
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i testsrc=size=320x240:rate=30:duration=10
        -pix_fmt yuv420p "${FIXTURE_10S}")
add_test(NAME make_fixture_static
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i color=c=black:size=320x240:rate=30:duration=6
        -pix_fmt yuv420p "${FIXTURE_STATIC}")
set_tests_properties(make_fixture_6s make_fixture_10s make_fixture_static
    PROPERTIES FIXTURES_SETUP VideoFixtures)

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

# Phase 4：FrameFilter 时间采样 + Y 平面帧差
add_executable(frame_filter_test frame_filter_test.cpp)
target_link_libraries(frame_filter_test PRIVATE edge_core)
edge_copy_runtime_dlls(frame_filter_test)
add_test(NAME frame_filter_test COMMAND frame_filter_test
    "${FIXTURE_6S}" "${FIXTURE_STATIC}")
set_tests_properties(frame_filter_test PROPERTIES FIXTURES_REQUIRED VideoFixtures)

# Phase 5：BoundedQueue
add_executable(bounded_queue_test bounded_queue_test.cpp)
target_link_libraries(bounded_queue_test PRIVATE edge_core)
edge_copy_runtime_dlls(bounded_queue_test)
add_test(NAME bounded_queue_test COMMAND bounded_queue_test)

# Phase 6：mtmd 单图推理（需要本地模型权重；缺失时测试失败）
add_executable(vlm_smoke_test vlm_smoke_test.cpp)
target_link_libraries(vlm_smoke_test PRIVATE edge_core)
edge_copy_runtime_dlls(vlm_smoke_test)
add_test(NAME vlm_smoke_test COMMAND vlm_smoke_test
    "${CMAKE_SOURCE_DIR}/models/InternVL3-1B-Instruct-Q8_0.gguf"
    "${CMAKE_SOURCE_DIR}/models/mmproj-InternVL3-1B-Instruct-Q8_0.gguf"
    "${FIXTURE_6S}")
set_tests_properties(vlm_smoke_test PROPERTIES FIXTURES_REQUIRED VideoFixtures TIMEOUT 900)

# Phase 7：Skill 加载与行为切换
add_executable(skill_test skill_test.cpp)
target_link_libraries(skill_test PRIVATE edge_core)
edge_copy_runtime_dlls(skill_test)
add_test(NAME skill_test COMMAND skill_test
    "${CMAKE_SOURCE_DIR}/models/InternVL3-1B-Instruct-Q8_0.gguf"
    "${CMAKE_SOURCE_DIR}/models/mmproj-InternVL3-1B-Instruct-Q8_0.gguf"
    "${FIXTURE_6S}"
    "${CMAKE_SOURCE_DIR}/skills/door-camera.md"
    "${CMAKE_SOURCE_DIR}/skills/pet-watch.md")
set_tests_properties(skill_test PROPERTIES FIXTURES_REQUIRED VideoFixtures TIMEOUT 900)

# Phase 8：结构化输出解析（纯单元测试，不需要模型）
add_executable(structured_output_test structured_output_test.cpp)
target_link_libraries(structured_output_test PRIVATE edge_core)
edge_copy_runtime_dlls(structured_output_test)
add_test(NAME structured_output_test COMMAND structured_output_test)

# Phase 8：ToolRegistry 白名单与内置工具
add_executable(tool_registry_test tool_registry_test.cpp)
target_link_libraries(tool_registry_test PRIVATE edge_core)
edge_copy_runtime_dlls(tool_registry_test)
add_test(NAME tool_registry_test COMMAND tool_registry_test)
