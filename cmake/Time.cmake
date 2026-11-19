# Builds the frame, simulation and loading timers without graphics SDKs.
include_guard(GLOBAL)
get_filename_component(IKIGAI_TIME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IKIGAI_TIME_DIR "${IKIGAI_TIME_ROOT}/src/engine/utilsModule/time")

add_library(ikigai_time STATIC
    "${IKIGAI_TIME_DIR}/time.cpp"
    "${IKIGAI_TIME_DIR}/time.h"
    "${IKIGAI_TIME_DIR}/loadTimer.cpp"
    "${IKIGAI_TIME_DIR}/loadTimer.h")
target_compile_features(ikigai_time PUBLIC cxx_std_20)
target_include_directories(ikigai_time PUBLIC "${IKIGAI_TIME_ROOT}/src/engine")
set_target_properties(ikigai_time PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    IKIGAI_SOURCE_DIRECTORY "${IKIGAI_TIME_DIR}")
if(MSVC)
    target_compile_options(ikigai_time PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(ikigai_time PRIVATE -Wall -Wextra -Wpedantic)
endif()
