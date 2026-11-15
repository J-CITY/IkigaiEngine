# Builds the math tests and optional diagnostic tools; never linked into production.
cmake_minimum_required(VERSION 3.20)
project(IkigaiMathTests LANGUAGES CXX)

enable_testing()

get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include("${ENGINE_ROOT}/cmake/Math.cmake")

add_executable(math_tests
	main.cpp
	unit/math/vector2.cpp
	unit/math/vector3.cpp
	unit/math/vector4.cpp
	unit/math/matrix3.cpp
	unit/math/matrix4.cpp
	unit/math/quaternion.cpp
	unit/math/aabb.cpp
	unit/math/curves.cpp)

# Exercise the production library and its propagated public requirements.
target_link_libraries(math_tests PRIVATE ikigai_math)
target_include_directories(math_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME math_module COMMAND math_tests)
set_tests_properties(math_module PROPERTIES TIMEOUT 30 LABELS unit)

if(WIN32)
    add_subdirectory(tools/crashReporter)
endif()
