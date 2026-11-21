# Builds the shared math implementation and publishes its header requirements.
# Every source is explicit; platform applications and tests link this same target.
include_guard(GLOBAL)
get_filename_component(IKIGAI_MATH_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(ikigai_math STATIC
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/curves.cpp"
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/curves.h"
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/geometry/frustumPlanes.cpp"
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/geometry/frustumPlanes.h"
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/math.h"
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/vector2.h"
    "${IKIGAI_MATH_ROOT}/src/engine/mathModule/geometry/aabb.h")
target_compile_features(ikigai_math PUBLIC cxx_std_20)
target_include_directories(ikigai_math PUBLIC "${IKIGAI_MATH_ROOT}/src/engine")
set_target_properties(ikigai_math PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    IKIGAI_SOURCE_DIRECTORY "${IKIGAI_MATH_ROOT}/src/engine/mathModule")
if(WIN32)
    target_compile_definitions(ikigai_math PUBLIC NOMINMAX)
endif()
if(MSVC)
    target_compile_options(ikigai_math PRIVATE /W4 /utf-8)
    target_compile_options(ikigai_math PUBLIC /EHsc)
elseif(ANDROID OR EMSCRIPTEN)
    # Public math operations throw; preserve that contract in cross builds.
    target_compile_options(ikigai_math PUBLIC -fexceptions)
endif()
