# 边缘多模态 Agent（MVP）
# 依赖接入遵循《依赖接入决策.md》：llama.cpp submodule 固定 v0.3.0；
# Windows FFmpeg 使用锁定的 gyan.dev shared 构建；Linux 在 Ubuntu 24.04
# build image 中通过 pkg-config 使用系统 FFmpeg 开发包。
# 任何依赖缺失必须 FATAL_ERROR，禁止静默回退。

cmake_minimum_required(VERSION 3.24)
project(edge_agent LANGUAGES CXX)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC：/Zc:__cplusplus 让 __cplusplus 反映真实标准版本（llama.cpp 的 LU8 宏依赖它）；
# /utf-8 使源码（含中文注释）按 UTF-8 解析。
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus)
endif()

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本项目支持 Windows 与 Linux；当前平台为 ${CMAKE_SYSTEM_NAME}")
endif()

# ---------------------------------------------------------------------------
# FFmpeg：Windows 使用锁定的 gyan.dev shared 构建；Linux 使用发行版开发包。
# ---------------------------------------------------------------------------
if(WIN32)
    set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
    find_path(FFMPEG_INCLUDE_DIR NAMES libavformat/avformat.h
        HINTS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH)
    set(_FFMPEG_MISSING "")
    foreach(lib avcodec avformat swscale avutil)
        find_library(FFMPEG_${lib}_LIBRARY NAMES ${lib}
            HINTS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        if(NOT FFMPEG_${lib}_LIBRARY)
            list(APPEND _FFMPEG_MISSING "${lib}")
        endif()
    endforeach()
    set(EDGE_FFMPEG_REQUIRED_DLLS
        avcodec-63.dll avformat-63.dll avutil-61.dll swscale-10.dll swresample-7.dll)
    foreach(dll IN LISTS EDGE_FFMPEG_REQUIRED_DLLS)
        find_file("FFMPEG_DLL_${dll}" NAMES ${dll}
            HINTS "${FFMPEG_ROOT}/bin" NO_DEFAULT_PATH)
        if(NOT FFMPEG_DLL_${dll})
            list(APPEND _FFMPEG_MISSING "bin/${dll}")
        endif()
    endforeach()
    if(NOT FFMPEG_INCLUDE_DIR OR _FFMPEG_MISSING)
        message(FATAL_ERROR
            "Windows FFmpeg dependency missing. Expected gyan.dev ffmpeg-9.0.1-full_build-shared.\n"
            "Expected location: ${FFMPEG_ROOT}; missing headers=${FFMPEG_INCLUDE_DIR}, libraries/DLLs=[${_FFMPEG_MISSING}].\n"
            "See 依赖接入决策.md for the pinned download URL, SHA-256, and restore steps.")
    endif()
    set(EDGE_FFMPEG_LIB_TO_DLL avcodec avcodec-63.dll avformat avformat-63.dll
        swscale swscale-10.dll avutil avutil-61.dll)
    foreach(lib avcodec avformat swscale avutil)
        add_library(ffmpeg::${lib} SHARED IMPORTED GLOBAL)
        list(FIND EDGE_FFMPEG_LIB_TO_DLL "${lib}" _idx)
        math(EXPR _dll_idx "${_idx} + 1")
        list(GET EDGE_FFMPEG_LIB_TO_DLL ${_dll_idx} _dll_name)
        set_target_properties(ffmpeg::${lib} PROPERTIES
            IMPORTED_IMPLIB "${FFMPEG_${lib}_LIBRARY}"
            IMPORTED_LOCATION "${FFMPEG_ROOT}/bin/${_dll_name}")
    endforeach()
    add_library(ffmpeg::headers INTERFACE IMPORTED GLOBAL)
    set_target_properties(ffmpeg::headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
    file(GLOB EDGE_FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
    set(FFMPEG_EXE "${FFMPEG_ROOT}/bin/ffmpeg.exe")
else()
    find_package(PkgConfig QUIET)
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR
            "Linux FFmpeg discovery requires pkg-config. Install pkg-config and the FFmpeg development packages "
            "(libavformat-dev, libavcodec-dev, libswscale-dev, libavutil-dev), then reconfigure.")
    endif()
    set(_FFMPEG_MISSING "")
    foreach(component avformat avcodec swscale avutil)
        pkg_check_modules(FFMPEG_${component} QUIET IMPORTED_TARGET "lib${component}")
        if(NOT FFMPEG_${component}_FOUND)
            list(APPEND _FFMPEG_MISSING "lib${component}")
        endif()
    endforeach()
    if(_FFMPEG_MISSING)
        message(FATAL_ERROR
            "Linux FFmpeg development dependencies are missing: ${_FFMPEG_MISSING}. "
            "Install pkg-config, libavformat-dev, libavcodec-dev, libswscale-dev, and libavutil-dev.")
    endif()
    foreach(component avformat avcodec swscale avutil)
        add_library(ffmpeg::${component} INTERFACE IMPORTED GLOBAL)
        set_target_properties(ffmpeg::${component} PROPERTIES
            INTERFACE_LINK_LIBRARIES "PkgConfig::FFMPEG_${component}")
    endforeach()
    add_library(ffmpeg::headers INTERFACE IMPORTED GLOBAL)
    set_target_properties(ffmpeg::headers PROPERTIES
        INTERFACE_LINK_LIBRARIES "PkgConfig::FFMPEG_avformat")
    find_program(FFMPEG_EXE NAMES ffmpeg)
    if(NOT FFMPEG_EXE)
        message(FATAL_ERROR
            "Linux FFmpeg executable is required to generate CTest video fixtures. "
            "Install the ffmpeg package and reconfigure.")
    endif()
endif()

function(edge_copy_runtime_dlls TARGET_NAME)
    if(WIN32)
        foreach(dll ${EDGE_FFMPEG_DLLS})
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" $<TARGET_FILE_DIR:${TARGET_NAME}>)
        endforeach()
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>
            COMMAND_EXPAND_LISTS)
    endif()
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

string(STRIP "${LLAMA_HEAD_COMMIT}" LLAMA_HEAD_COMMIT)
if(_LLAMA_REV_RESULT EQUAL 0)
    string(LENGTH "${LLAMA_HEAD_COMMIT}" _LLAMA_COMMIT_LEN)
endif()

# git 不可用、rev-parse 失败或输出不是 40 位 commit：一律 FATAL_ERROR，不静默继续
if(NOT _LLAMA_REV_RESULT EQUAL 0)
    message(FATAL_ERROR
        "无法校验 llama.cpp submodule 的 commit（git 命令执行失败，"
        "result=${_LLAMA_REV_RESULT}）。\n"
        "期望 commit：c1d0e7a004015f23bc0233470b747b596f29b264（tag v0.3.0）\n"
        "期望位置：${LLAMA_DIR}\n"
        "恢复步骤：确认 git 可用后执行 git -C third_party/llama.cpp checkout "
        "c1d0e7a004015f23bc0233470b747b596f29b264")
endif()
if(NOT LLAMA_HEAD_COMMIT STREQUAL "c1d0e7a004015f23bc0233470b747b596f29b264")
    message(FATAL_ERROR
        "llama.cpp HEAD 为 ${LLAMA_HEAD_COMMIT}（长度 ${_LLAMA_COMMIT_LEN}），"
        "与锁定的 v0.3.0 commit 不符。\n"
        "恢复步骤：git -C third_party/llama.cpp checkout "
        "c1d0e7a004015f23bc0233470b747b596f29b264")
endif()

set(LLAMA_BUILD_COMMON  OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE) # Windows 运行 DLL 经 TARGET_RUNTIME_DLLS 复制
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # Linux 静态链接 llama/ggml，简化二进制交付
endif()
set(LLAMA_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER  OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_MTMD    ON  CACHE BOOL "" FORCE) # 独立构建 tools/mtmd 库
if(NOT WIN32)
    set(GGML_CUDA OFF CACHE BOOL "" FORCE)
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE) # 发布二进制不绑定构建机的 CPU 指令集
endif()
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
