# edge_agent 主程序
add_executable(edge_agent main.cpp)
target_include_directories(edge_agent PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include"
    "${CMAKE_SOURCE_DIR}/third_party/llama.cpp/tools/mtmd")
target_link_libraries(edge_agent PRIVATE nlohmann_json ffmpeg::headers llama mtmd)
edge_copy_runtime_dlls(edge_agent)
