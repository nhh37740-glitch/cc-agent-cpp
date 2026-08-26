# 边缘多模态 Agent（MVP）
# 依赖接入遵循《依赖接入决策.md》：llama.cpp submodule 固定 v0.3.0；
# FFmpeg 使用 gyan.dev ffmpeg-9.0.1-full_build-shared 解压到 third_party/ffmpeg。
# 任何依赖缺失必须 FATAL_ERROR，禁止静默回退。

cmake_minimum_required(VERSION 3.24)
project(edge_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC：/Zc:__cplusplus 让 __cplusplus 反映真实标准版本（llama.cpp 的 LU8 宏依赖它）；
# /utf-8 使源码（含中文注释）按 UTF-8 解析。
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "本项目当前仅在 Windows 上构建与验收")
endif()

# ---------------------------------------------------------------------------
# FFmpeg（gyan.dev shared 构建导入）
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")

find_path(FFMPEG_INCLUDE_DIR
    NAMES libavformat/avformat.h
    HINTS "${FFMPEG_ROOT}/include"
    NO_DEFAULT_PATH)

set(_FFMPEG_MISSING "")
foreach(lib avcodec avformat swscale avutil)
    find_library(FFMPEG_${lib}_LIBRARY
        NAMES ${lib}
        HINTS "${FFMPEG_ROOT}/lib"
        NO_DEFAULT_PATH)
    if(NOT FFMPEG_${lib}_LIBRARY)
        list(APPEND _FFMPEG_MISSING "${lib}")
    endif()
endforeach()

if(NOT FFMPEG_INCLUDE_DIR OR _FFMPEG_MISSING)
    message(FATAL_ERROR
        "FFmpeg 依赖缺失。期望版本：ffmpeg-9.0.1-full_build-shared（gyan.dev）。\n"
        "期望位置：${FFMPEG_ROOT}（需要 include/ 与 lib/ 子目录，含 avcodec.lib、avformat.lib、swscale.lib、avutil.lib 导入库）。\n"
        "缺失项：头文件=${FFMPEG_INCLUDE_DIR}；导入库=[${_FFMPEG_MISSING}]\n"
        "恢复步骤：按《依赖接入决策.md》下载 "
        "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.1-full_build-shared.7z "
        "(SHA-256 cb4d5e8db6a3353bffdb2100d3eb4b76733457fa443215e236f57c99f9ffdca4)，"
        "解压后把其中的 include/、lib/、bin/ 复制到 third_party/ffmpeg/ 下。")
endif()

message(STATUS "FFmpeg 头文件: ${FFMPEG_INCLUDE_DIR}")
foreach(lib avcodec avformat swscale avutil)
    message(STATUS "FFmpeg ${lib}: ${FFMPEG_${lib}_LIBRARY}")
endforeach()

foreach(lib avcodec avformat swscale avutil)
    add_library(ffmpeg::${lib} SHARED IMPORTED GLOBAL)
    set_target_properties(ffmpeg::${lib} PROPERTIES
        IMPORTED_IMPLIB   "${FFMPEG_${lib}_LIBRARY}"
        IMPORTED_LOCATION "${FFMPEG_ROOT}/bin")
endforeach()
add_library(ffmpeg::headers INTERFACE IMPORTED GLOBAL)
set_target_properties(ffmpeg::headers PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")

# 运行期 DLL 一律复制到可执行文件输出目录，不依赖 PATH。
file(GLOB EDGE_FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")

function(edge_copy_runtime_dlls TARGET_NAME)
    # FFmpeg 运行 DLL
    if(EDGE_FFMPEG_DLLS)
        foreach(dll ${EDGE_FFMPEG_DLLS})
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" $<TARGET_FILE_DIR:${TARGET_NAME}>)
        endforeach()
    endif()
    # llama/ggml/mtmd 等共享运行库
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>
        COMMAND_EXPAND_LISTS)
endfunction()

# ---------------------------------------------------------------------------
# llama.cpp（submodule，固定 v0.3.0 / c1d0e7a004015f23bc0233470b747b596f29b264）
# ---------------------------------------------------------------------------
set(LLAMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp")

if(NOT EXISTS "${LLAMA_DIR}/CMakeLists.txt" OR NOT EXISTS "${LLAMA_DIR}/tools/mtmd/mtmd.h")
    message(FATAL_ERROR
        "llama.cpp submodule 缺失或不含 mtmd。期望版本：v0.3.0 "
        "(commit c1d0e7a004015f23bc0233470b747b596f29b264)。\n"
        "恢复步骤：git submodule update --init --recursive；"
        "然后 git -C third_party/llama.cpp checkout "
        "c1d0e7a004015f23bc0233470b747b596f29b264")
endif()

execute_process(
    COMMAND git -C "${LLAMA_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE LLAMA_HEAD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _LLAMA_REV_RESULT)

if(_LLAMA_REV_RESULT EQUAL 0 AND NOT LLAMA_HEAD_COMMIT STREQUAL "c1d0e7a004015f23bc0233470b747b596f29b264")
    message(FATAL_ERROR
        "llama.cpp HEAD 为 ${LLAMA_HEAD_COMMIT}，与锁定的 v0.3.0 commit 不符。\n"
        "恢复步骤：git -C third_party/llama.cpp checkout "
        "c1d0e7a004015f23bc0233470b747b596f29b264")
endif()

set(LLAMA_BUILD_COMMON  OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   ON  CACHE BOOL "" FORCE) # llama/ggml/mtmd 构建为 DLL，经 TARGET_RUNTIME_DLLS 复制
set(LLAMA_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER  OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_MTMD    ON  CACHE BOOL "" FORCE) # 独立构建 tools/mtmd 库
set(LLAMA_LLGUIDANCE    OFF CACHE BOOL "" FORCE)
add_subdirectory("${LLAMA_DIR}" llama.cpp-build EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# 本项目源码
# ---------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
