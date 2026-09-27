# Public headers and third-party headers are shared by the project modules.
function(edge_configure_module TARGET_NAME)
    target_include_directories(${TARGET_NAME} PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include"
        "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd")
endfunction()

add_library(edge_video STATIC
    video/ffmpeg_video_source.cpp
    video/realtime_pacing_source.cpp
    video/yuv_to_rgb.cpp)
edge_configure_module(edge_video)
target_link_libraries(edge_video PUBLIC ffmpeg::headers ffmpeg::avformat ffmpeg::avcodec ffmpeg::swscale ffmpeg::avutil)

add_library(edge_filter STATIC filter/frame_filter.cpp)
edge_configure_module(edge_filter)
target_link_libraries(edge_filter PUBLIC edge_video)

add_library(edge_model STATIC
    model/llama_backend.cpp
    model/llama_vlm.cpp
    model/llama_text.cpp
    model/tool_decision_model.cpp)
edge_configure_module(edge_model)
target_link_libraries(edge_model PUBLIC nlohmann_json llama mtmd)

add_library(edge_skill STATIC skill/skill_loader.cpp)
edge_configure_module(edge_skill)

add_library(edge_agent_core STATIC
    agent/structured_output.cpp
    agent/builtin_tools.cpp
    agent/agent.cpp)
edge_configure_module(edge_agent_core)
target_link_libraries(edge_agent_core PUBLIC edge_model edge_skill nlohmann_json)

add_library(edge_dashboard STATIC web/dashboard.cpp)
edge_configure_module(edge_dashboard)
target_link_libraries(edge_dashboard PUBLIC edge_agent_core edge_video Threads::Threads)
if(WIN32)
    target_link_libraries(edge_dashboard PUBLIC ws2_32)
endif()

add_library(edge_pipeline STATIC app/pipeline.cpp)
edge_configure_module(edge_pipeline)
target_link_libraries(edge_pipeline PUBLIC
    edge_video edge_filter edge_model edge_skill edge_agent_core edge_dashboard)

# Compatibility aggregate for existing integration tests and downstream clients.
add_library(edge_core INTERFACE)
target_link_libraries(edge_core INTERFACE edge_pipeline)

# edge_agent 主程序
add_executable(edge_agent main.cpp)
edge_configure_module(edge_agent)
target_link_libraries(edge_agent PRIVATE edge_pipeline)
edge_copy_runtime_dlls(edge_agent)
