# Main-thread C++20 coroutines; independent of window, rendering and worker pools.
include_guard(GLOBAL)
get_filename_component(IKIGAI_COROUTINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IKIGAI_COROUTINE_DIR "${IKIGAI_COROUTINE_ROOT}/src/engine/coroutineModule")
add_library(ikigai_coroutines STATIC
    "${IKIGAI_COROUTINE_DIR}/scheduler.cpp"
    "${IKIGAI_COROUTINE_DIR}/coroutineScope.cpp"
    "${IKIGAI_COROUTINE_DIR}/scheduler.h"
    "${IKIGAI_COROUTINE_DIR}/coroutineScope.h"
    "${IKIGAI_COROUTINE_DIR}/task.h"
    "${IKIGAI_COROUTINE_DIR}/taskHandle.h"
    "${IKIGAI_COROUTINE_DIR}/awaitables.h")
target_compile_features(ikigai_coroutines PUBLIC cxx_std_20)
target_include_directories(ikigai_coroutines PUBLIC "${IKIGAI_COROUTINE_ROOT}/src/engine")
set_target_properties(ikigai_coroutines PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    IKIGAI_SOURCE_DIRECTORY "${IKIGAI_COROUTINE_DIR}")
if(MSVC)
    target_compile_options(ikigai_coroutines PRIVATE /W4 /utf-8)
    target_compile_options(ikigai_coroutines PUBLIC /EHsc)
else()
    target_compile_options(ikigai_coroutines PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(EMSCRIPTEN OR ANDROID)
    target_compile_options(ikigai_coroutines PUBLIC -fexceptions)
endif()
if(EMSCRIPTEN)
    target_link_options(ikigai_coroutines INTERFACE -fexceptions)
endif()
