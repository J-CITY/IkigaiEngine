include_guard(GLOBAL)

option(IKIGAI_ENABLE_TRACY "Build the engine with on-demand Tracy tracing" OFF)
add_library(ikigai_trace INTERFACE)

if(IKIGAI_ENABLE_TRACY)
    if(EMSCRIPTEN)
        message(FATAL_ERROR "IKIGAI_ENABLE_TRACY requires a native target")
    endif()

    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    # Use this same release for the capture and export tools.
    FetchContent_Declare(ikigai_tracy
        URL https://codeload.github.com/wolfpld/tracy/tar.gz/refs/tags/v0.14.1
        URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
    )

    function(ikigai_add_tracy_client)
        set(TRACY_ENABLE ON)
        set(TRACY_ON_DEMAND ON)
        set(TRACY_STATIC ON)
        set(TRACY_ONLY_LOCALHOST ON)
        set(TRACY_NO_BROADCAST ON)
        set(TRACY_NO_SYSTEM_TRACING ON)
        set(TRACY_NO_CRASH_HANDLER ON)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        FetchContent_MakeAvailable(ikigai_tracy)
    endfunction()
    ikigai_add_tracy_client()

    set_target_properties(TracyClient PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(ikigai_trace INTERFACE Tracy::TracyClient)
    target_compile_definitions(ikigai_trace INTERFACE IKIGAI_ENABLE_TRACY)
endif()
