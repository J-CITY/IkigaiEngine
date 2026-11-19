# Builds production logging channels, outputs and the stream-based logging API.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Services.cmake")
get_filename_component(IKIGAI_LOGGING_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IKIGAI_LOGGING_DIR "${IKIGAI_LOGGING_ROOT}/src/engine/utilsModule/log")

add_library(ikigai_logging STATIC
    "${IKIGAI_LOGGING_DIR}/logger.cpp"
    "${IKIGAI_LOGGING_DIR}/logger.h"
    "${IKIGAI_LOGGING_DIR}/loggerDefine.h")
target_compile_features(ikigai_logging PUBLIC cxx_std_20)
target_include_directories(ikigai_logging PUBLIC "${IKIGAI_LOGGING_ROOT}/src/engine")
target_link_libraries(ikigai_logging PUBLIC ikigai_services)
set_target_properties(ikigai_logging PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    IKIGAI_SOURCE_DIRECTORY "${IKIGAI_LOGGING_DIR}")
if(MSVC)
    target_compile_options(ikigai_logging PRIVATE /W4 /utf-8)
    target_compile_options(ikigai_logging PUBLIC /EHsc)
else()
    target_compile_options(ikigai_logging PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(ANDROID OR EMSCRIPTEN)
    target_compile_options(ikigai_logging PUBLIC -fexceptions)
endif()
if(EMSCRIPTEN)
    target_link_options(ikigai_logging INTERFACE -fexceptions)
else()
    # Native logging uses mutexes; do not enable Web worker threads implicitly.
    find_package(Threads REQUIRED)
    target_link_libraries(ikigai_logging PUBLIC Threads::Threads)
endif()
if(ANDROID)
    # Both the implementation and the public IKIGAI_COUT macro call logcat.
    find_library(IKIGAI_LOGGING_ANDROID_LOG_LIBRARY log REQUIRED)
    target_link_libraries(ikigai_logging PUBLIC "${IKIGAI_LOGGING_ANDROID_LOG_LIBRARY}")
endif()
