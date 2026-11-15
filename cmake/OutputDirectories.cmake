# Runtime artifacts always live below a configuration-specific output directory.
include_guard(GLOBAL)
get_filename_component(IKIGAI_OUTPUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

macro(ikigai_configure_output_directories platform)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
    endif()
    set(ENGINE_RUNTIME_OUTPUT_DIRECTORY "${IKIGAI_OUTPUT_ROOT}/out/${platform}" CACHE PATH
        "Base directory for runtime artifacts; each configuration has its own subdirectory")
    if(NOT IS_ABSOLUTE "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}")
        get_filename_component(ENGINE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}" ABSOLUTE
            BASE_DIR "${IKIGAI_OUTPUT_ROOT}")
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
endmacro()
