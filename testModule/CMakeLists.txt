# Standalone tests and diagnostic tools.
cmake_minimum_required(VERSION 3.20)
project(IkigaiTests LANGUAGES CXX)

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

include("${ENGINE_ROOT}/cmake/Logging.cmake")
include("${ENGINE_ROOT}/cmake/Memory.cmake")
include("${ENGINE_ROOT}/cmake/Task.cmake")
set(IKIGAI_SOURCE_ROOT "${ENGINE_ROOT}/src")
include("${ENGINE_ROOT}/cmake/modules/ecsModule.cmake")
add_library(ikigai_ecs_under_test STATIC
    ${IKIGAI_MANIFEST_ecsModule}
    "${IKIGAI_SOURCE_ROOT}/engine/resourceModule/serviceManager.cpp"
    "${IKIGAI_SOURCE_ROOT}/engine/utilsModule/weakPtr.cpp")
target_include_directories(ikigai_ecs_under_test PUBLIC "${IKIGAI_SOURCE_ROOT}/engine")
target_link_libraries(ikigai_ecs_under_test PUBLIC ikigai_memory PRIVATE ikigai_logging ikigai_task)

add_executable(ecs_tests
    unit/ecs/main.cpp
    unit/ecs/storage.cpp
    unit/ecs/systems.cpp)
target_include_directories(ecs_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ecs_tests PRIVATE ikigai_ecs_under_test ikigai_task)
add_test(NAME ecs_module COMMAND ecs_tests)
set_tests_properties(ecs_module PROPERTIES TIMEOUT 30 LABELS unit)

add_executable(ecs_demo_smoke
    smoke/ecs/main.cpp
    "${IKIGAI_SOURCE_ROOT}/ecsDemo/example.cpp"
    "${IKIGAI_SOURCE_ROOT}/ecsDemo/systems.cpp")
target_include_directories(ecs_demo_smoke PRIVATE "${IKIGAI_SOURCE_ROOT}")
target_link_libraries(ecs_demo_smoke PRIVATE ikigai_ecs_under_test)
add_test(NAME ecs_demo COMMAND ecs_demo_smoke)
set_tests_properties(ecs_demo PROPERTIES TIMEOUT 30 LABELS smoke)

option(IKIGAI_BUILD_ECS_BENCHMARKS "Build comparative ECS benchmarks from local dependency checkouts" OFF)
if(IKIGAI_BUILD_ECS_BENCHMARKS)
    add_subdirectory(benchmarks/ecs)
endif()

if(WIN32)
    add_subdirectory(tools/crashReporter)
endif()
