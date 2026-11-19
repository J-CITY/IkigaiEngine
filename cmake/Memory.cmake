# Builds allocators, frame arenas and CPU allocation tracking.
# All consumers share the tracking definition because it changes inline code and layouts.
include_guard(GLOBAL)
option(IKIGAI_ENABLE_MEMORY_TRACKING "Track engine CPU/GPU allocations; snapshots are captured on demand" OFF)
option(IKIGAI_MEMORY_TESTS "Legacy allocator test attachment (configure testModule separately)" OFF)
if(IKIGAI_MEMORY_TESTS)
    message(FATAL_ERROR "IKIGAI_MEMORY_TESTS no longer attaches tests to production. Configure the optional testModule separately.")
endif()

get_filename_component(IKIGAI_MEMORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IKIGAI_MEMORY_DIR "${IKIGAI_MEMORY_ROOT}/src/engine/utilsModule/memory")
if(NOT TARGET ikigai_memory)
    add_library(ikigai_memory STATIC
        "${IKIGAI_MEMORY_DIR}/memory.cpp"
        "${IKIGAI_MEMORY_DIR}/frameArena.cpp"
        "${IKIGAI_MEMORY_DIR}/allocationTracker.cpp"
        "${IKIGAI_MEMORY_DIR}/allocationTracker.h"
        "${IKIGAI_MEMORY_DIR}/allocator.h"
        "${IKIGAI_MEMORY_DIR}/frameArena.h"
        "${IKIGAI_MEMORY_DIR}/freeListAllocator.h"
        "${IKIGAI_MEMORY_DIR}/heapAllocator.h"
        "${IKIGAI_MEMORY_DIR}/linearAllocator.h"
        "${IKIGAI_MEMORY_DIR}/memory.h"
        "${IKIGAI_MEMORY_DIR}/monotonicAllocator.h"
        "${IKIGAI_MEMORY_DIR}/objectPool.h"
        "${IKIGAI_MEMORY_DIR}/poolAllocator.h"
        "${IKIGAI_MEMORY_DIR}/profiledAllocator.h"
        "${IKIGAI_MEMORY_DIR}/resourceAllocator.h"
        "${IKIGAI_MEMORY_DIR}/stackAllocator.h")
    target_compile_features(ikigai_memory PUBLIC cxx_std_20)
    target_include_directories(ikigai_memory PUBLIC "${IKIGAI_MEMORY_ROOT}/src/engine")
    target_compile_definitions(ikigai_memory PUBLIC
        IKIGAI_ENABLE_MEMORY_TRACKING=$<BOOL:${IKIGAI_ENABLE_MEMORY_TRACKING}>)
    set_target_properties(ikigai_memory PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        IKIGAI_SOURCE_DIRECTORY "${IKIGAI_MEMORY_DIR}")
    if(WIN32)
        # Public allocators use std::max/numeric_limits even after windows.h.
        target_compile_definitions(ikigai_memory PUBLIC NOMINMAX)
    endif()
    if(MSVC)
        target_compile_options(ikigai_memory PRIVATE /W4 /utf-8)
        target_compile_options(ikigai_memory PUBLIC /EHsc)
    else()
        target_compile_options(ikigai_memory PRIVATE -Wall -Wextra)
    endif()
    if(ANDROID OR EMSCRIPTEN)
        target_compile_options(ikigai_memory PUBLIC -fexceptions)
    endif()
    if(EMSCRIPTEN)
        target_link_options(ikigai_memory INTERFACE -fexceptions)
    endif()
endif()

# Compatibility for existing consumers; source ownership is handled by EngineCore.
# Production never imports local test scripts or attaches test-only implementations.
function(ikigai_configure_memory target)
    target_link_libraries(${target} PRIVATE ikigai_memory)
endfunction()
