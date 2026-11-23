# Reports effective engine options without compiler checks or third-party setup.
# run_vs.py consumes the marked JSON line; normal CMake uses EngineOptions directly.
cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/EngineOptions.cmake")
ikigai_configure_engine_options("${IKIGAI_PLATFORM}")
set(_ikigai_options "{}")
foreach(_ikigai_option USE_LUA USE_IKIGAI_SCRIPT USE_AI USE_CAPTURE USE_PROFILER USE_EDITOR USE_FILE_WATCHER USE_OPENGL USE_VULKAN USE_DX12 USE_METAL
        USE_DLSS USE_FSR USE_DLSS_FRAME_GENERATION USE_FSR_FRAME_GENERATION USE_STREAMLINE
        USE_NETWORK IKIGAI_NETWORK_TLS)
    if(${_ikigai_option})
        set(_ikigai_value true)
    else()
        set(_ikigai_value false)
    endif()
    string(JSON _ikigai_options SET "${_ikigai_options}" "${_ikigai_option}" "${_ikigai_value}")
endforeach()
string(JSON _ikigai_options SET "${_ikigai_options}" IKIGAI_PLATFORM "\"${IKIGAI_OPTIONS_PLATFORM}\"")
string(REPLACE "\n" "" _ikigai_options "${_ikigai_options}")
message("IKIGAI_OPTIONS_JSON=${_ikigai_options}")
