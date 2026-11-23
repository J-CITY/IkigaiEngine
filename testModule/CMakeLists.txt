# Standalone tests and diagnostic tools.
cmake_minimum_required(VERSION 3.20)
project(IkigaiTests LANGUAGES CXX)

enable_testing()

get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include("${ENGINE_ROOT}/cmake/Math.cmake")
include("${ENGINE_ROOT}/cmake/JsonSerde.cmake")

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
target_link_libraries(math_tests PRIVATE ikigai_math ikigai_json_serde)
target_include_directories(math_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME math_module COMMAND math_tests)
set_tests_properties(math_module PROPERTIES TIMEOUT 30 LABELS unit)

include("${ENGINE_ROOT}/cmake/Logging.cmake")
include("${ENGINE_ROOT}/cmake/Memory.cmake")
include("${ENGINE_ROOT}/cmake/Task.cmake")
include("${ENGINE_ROOT}/cmake/Coroutines.cmake")
set(IKIGAI_SOURCE_ROOT "${ENGINE_ROOT}/src")
include("${ENGINE_ROOT}/cmake/modules/ecsModule.cmake")
add_library(ikigai_ecs_under_test STATIC ${IKIGAI_MANIFEST_ecsModule})
target_include_directories(ikigai_ecs_under_test PUBLIC "${IKIGAI_SOURCE_ROOT}/engine")
target_link_libraries(ikigai_ecs_under_test PUBLIC ikigai_memory PRIVATE ikigai_logging)

add_executable(ecs_tests
    unit/ecs/main.cpp
    unit/ecs/storage.cpp
    unit/ecs/systems.cpp
    "${IKIGAI_SOURCE_ROOT}/engine/coreModule/ecs/systemBatchExecutor.cpp")
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

include("${ENGINE_ROOT}/cmake/modules/aiModule.cmake")
include("${ENGINE_ROOT}/cmake/Services.cmake")
set(ai_sources)
foreach(ai_file IN LISTS IKIGAI_MANIFEST_aiModule)
	if(ai_file MATCHES "\\.cpp$")
		list(APPEND ai_sources "${ai_file}")
	endif()
endforeach()
add_library(ikigai_ai_under_test STATIC
	${ai_sources}
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/expressions/expression.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/serialization/typedDescriptor.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/variables/entityRef.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/variables/variableContext.cpp")
target_include_directories(ikigai_ai_under_test PUBLIC "${IKIGAI_SOURCE_ROOT}/engine")
target_link_libraries(ikigai_ai_under_test PUBLIC ikigai_math ikigai_json_serde ikigai_services)
target_compile_features(ikigai_ai_under_test PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(ikigai_ai_under_test PRIVATE /W4 /utf-8 /EHsc)
endif()

add_library(ikigai_particles_under_test STATIC "${IKIGAI_SOURCE_ROOT}/engine/particleModule/particles.cpp")
target_include_directories(ikigai_particles_under_test PUBLIC "${IKIGAI_SOURCE_ROOT}/engine")
target_link_libraries(ikigai_particles_under_test PUBLIC ikigai_math)
target_compile_features(ikigai_particles_under_test PUBLIC cxx_std_20)
if(MSVC)
	target_compile_options(ikigai_particles_under_test PRIVATE /W4 /utf-8 /EHsc)
endif()

include("${ENGINE_ROOT}/cmake/Physics2D.cmake")
include("${ENGINE_ROOT}/cmake/Physics3D.cmake")
ikigai_add_physics2d_library()
ikigai_add_physics3d_library()

function(ikigai_add_module_test target source library)
	add_executable(${target} "${source}")
	target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(${target} PRIVATE ${library})
	target_compile_features(${target} PRIVATE cxx_std_20)
	if(MSVC)
		target_compile_options(${target} PRIVATE /utf-8 /EHsc)
	endif()
	add_test(NAME ${target} COMMAND ${target})
	set_tests_properties(${target} PROPERTIES TIMEOUT 30 LABELS unit)
endfunction()

ikigai_add_module_test(ai_module unit/ai/main.cpp ikigai_ai_under_test)
ikigai_add_module_test(coroutine_module unit/coroutine/main.cpp ikigai_coroutines)
ikigai_add_module_test(particle_module unit/particle/main.cpp ikigai_particles_under_test)
ikigai_add_module_test(reflection_module unit/reflection/main.cpp ikigai_json_serde)
target_link_libraries(reflection_module PRIVATE ikigai_math)
ikigai_add_module_test(task_module unit/task/main.cpp ikigai_task)
ikigai_add_module_test(utils_module unit/utils/main.cpp ikigai_math)
target_sources(utils_module PRIVATE "${IKIGAI_SOURCE_ROOT}/engine/utilsModule/stringUtils.cpp")
ikigai_add_module_test(physics2d_module unit/physics2d/main.cpp ikigai_physics2d)
ikigai_add_module_test(physics_module unit/physics/main.cpp ikigai_physics3d)

option(IKIGAI_BUILD_ECS_BENCHMARKS "Build comparative ECS benchmarks from local dependency checkouts" OFF)
if(IKIGAI_BUILD_ECS_BENCHMARKS)
    add_subdirectory(benchmarks/ecs)
endif()

if(WIN32)
    add_subdirectory(tools/crashReporter)
endif()
