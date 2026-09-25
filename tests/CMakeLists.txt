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
# 非零首 PTS fixture（R-025；负 PTS 用合成源在测试内验证，MP4 无法存储）
set(FIXTURE_OFFSET "${FIXTURE_DIR}/offset_plus5.mp4")
add_test(NAME make_fixture_offset
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i "testsrc=size=160x90:rate=10:duration=1"
        -output_ts_offset 5 -pix_fmt yuv420p "${FIXTURE_OFFSET}")
set_tests_properties(make_fixture_offset PROPERTIES FIXTURES_SETUP VideoFixtures)

add_test(NAME pacing_test COMMAND pacing_test
    "${FIXTURE_10S}" "${FIXTURE_OFFSET}")
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
    "${CMAKE_SOURCE_DIR}/tests/data/skill-door-short.md"
    "${CMAKE_SOURCE_DIR}/tests/data/skill-pet-short.md")
set_tests_properties(skill_test PROPERTIES TIMEOUT 900)

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

# Phase 9：Agent Loop（MockModel，不需要真实模型）
add_executable(agent_loop_test agent_loop_test.cpp)
target_link_libraries(agent_loop_test PRIVATE edge_core)
edge_copy_runtime_dlls(agent_loop_test)
add_test(NAME agent_loop_test COMMAND agent_loop_test
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Phase 10：长时间运行内存稳定性（30s fixture，视频管线无模型）
set(FIXTURE_30S "${FIXTURE_DIR}/testsrc_30s.mp4")
add_test(NAME make_fixture_30s
    COMMAND "${FFMPEG_EXE}" -y -loglevel error
        -f lavfi -i "testsrc=size=320x240:rate=30:duration=30"
        -pix_fmt yuv420p "${FIXTURE_30S}")
set_tests_properties(make_fixture_30s PROPERTIES FIXTURES_SETUP VideoFixtures)

add_executable(memory_test memory_test.cpp)
target_link_libraries(memory_test PRIVATE edge_core)
edge_copy_runtime_dlls(memory_test)
add_test(NAME memory_test COMMAND memory_test "${FIXTURE_30S}")
set_tests_properties(memory_test PROPERTIES FIXTURES_REQUIRED VideoFixtures TIMEOUT 300)

# R-029：日志并发写串行化
add_executable(log_serialization_test log_serialization_test.cpp)
target_link_libraries(log_serialization_test PRIVATE edge_core)
edge_copy_runtime_dlls(log_serialization_test)
add_test(NAME log_serialization_test COMMAND log_serialization_test)

# R-030：sampler 重置确定性（真实模型）
add_executable(determinism_test determinism_test.cpp)
target_link_libraries(determinism_test PRIVATE edge_core)
edge_copy_runtime_dlls(determinism_test)
add_test(NAME determinism_test COMMAND determinism_test
    "${CMAKE_SOURCE_DIR}/models/InternVL3-1B-Instruct-Q8_0.gguf"
    "${CMAKE_SOURCE_DIR}/models/mmproj-InternVL3-1B-Instruct-Q8_0.gguf"
    "${FIXTURE_6S}")
set_tests_properties(determinism_test PROPERTIES FIXTURES_REQUIRED VideoFixtures TIMEOUT 900)

# R-021/R-023/R-031：CLI 行为（退出码与诊断）
add_executable(cli_args_test cli_args_test.cpp)
target_link_libraries(cli_args_test PRIVATE edge_core)
edge_copy_runtime_dlls(cli_args_test)
add_test(NAME cli_args_test COMMAND cli_args_test $<TARGET_FILE:edge_agent>)
set_tests_properties(cli_args_test PROPERTIES TIMEOUT 300)

# R-028：五类依赖缺失场景的隔离配置测试
add_test(NAME dependency_error_tests
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_SOURCE_DIR}/tests/dependency_error_tests.ps1"
        -Cmake "${CMAKE_COMMAND}"
        -Source "${CMAKE_SOURCE_DIR}"
        -WorkRoot "$ENV{TEMP}/edge_dep_test_$PID")
set_tests_properties(dependency_error_tests PROPERTIES TIMEOUT 900)

# 远程 Web 面板状态、帧编码与有界历史
add_executable(dashboard_test dashboard_test.cpp)
target_link_libraries(dashboard_test PRIVATE edge_core)
edge_copy_runtime_dlls(dashboard_test)
add_test(NAME dashboard_test COMMAND dashboard_test)

# 视觉事实规范化：允许明确实体词，拒绝字段错配，避免丢弃可执行事实。
add_executable(visual_checklist_test visual_checklist_test.cpp)
target_link_libraries(visual_checklist_test PRIVATE edge_core)
edge_copy_runtime_dlls(visual_checklist_test)
add_test(NAME visual_checklist_test COMMAND visual_checklist_test)

# 双模型新增：Qwen 文本模型必须独立完成工具决策，不接收图像。
add_executable(decision_model_test decision_model_test.cpp)
target_link_libraries(decision_model_test PRIVATE edge_core)
edge_copy_runtime_dlls(decision_model_test)
add_test(NAME decision_model_test COMMAND decision_model_test
    "${CMAKE_SOURCE_DIR}/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
    "${CMAKE_SOURCE_DIR}/skills/door-camera.md")
set_tests_properties(decision_model_test PROPERTIES TIMEOUT 900)
