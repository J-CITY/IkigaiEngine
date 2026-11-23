# Standalone tests and diagnostic tools.
cmake_minimum_required(VERSION 3.20)
project(IkigaiTests LANGUAGES CXX)

enable_testing()

add_library(ikigai_test_support STATIC support/cases.cpp)
target_include_directories(ikigai_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ikigai_test_support PUBLIC cxx_std_20)

function(ikigai_register_test name target)
	add_test(NAME ${name} COMMAND ${target} ${ARGN})
	set_tests_properties(${name} PROPERTIES TIMEOUT 30 LABELS unit)
endfunction()

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
target_link_libraries(math_tests PRIVATE ikigai_math ikigai_json_serde ikigai_test_support)

ikigai_register_test(math_module math_tests basics)
ikigai_register_test(math_matrix3 math_tests matrix3)
ikigai_register_test(math_matrix4 math_tests matrix4)
ikigai_register_test(math_quaternion math_tests quaternion)

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
target_link_libraries(ecs_tests PRIVATE ikigai_ecs_under_test ikigai_task ikigai_test_support)
ikigai_register_test(ecs_module ecs_tests storage)
ikigai_register_test(ecs_systems ecs_tests systems)

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
	target_link_libraries(${target} PRIVATE ${library} ikigai_test_support)
	target_compile_features(${target} PRIVATE cxx_std_20)
	if(MSVC)
		target_compile_options(${target} PRIVATE /utf-8 /EHsc)
	endif()
	ikigai_register_test(${target} ${target} ${ARGN})
endfunction()

ikigai_add_module_test(ai_module unit/ai/main.cpp ikigai_ai_under_test basics)
ikigai_register_test(ai_selector ai_module selector)
ikigai_register_test(ai_paths ai_module paths)
ikigai_add_module_test(coroutine_module unit/coroutine/main.cpp ikigai_coroutines basics)
ikigai_register_test(coroutine_child_cancellation coroutine_module child_cancellation)
ikigai_register_test(coroutine_scope coroutine_module scope)
ikigai_register_test(coroutine_budget coroutine_module budget)
ikigai_add_module_test(particle_module unit/particle/main.cpp ikigai_particles_under_test basics)
ikigai_register_test(particle_emission_and_drain particle_module emission_and_drain)
ikigai_register_test(particle_seeded_motion_3d particle_module seeded_motion_3d)
ikigai_add_module_test(reflection_module unit/reflection/main.cpp ikigai_json_serde basics)
ikigai_register_test(reflection_registration reflection_module registration)
target_link_libraries(reflection_module PRIVATE ikigai_math)
ikigai_add_module_test(task_module unit/task/main.cpp ikigai_task basics)
ikigai_register_test(task_parallel_for task_module parallel_for)
ikigai_register_test(task_batch_dependencies task_module batch_dependencies)
ikigai_register_test(task_background_cancellation task_module background_cancellation)
ikigai_add_module_test(utils_module unit/utils/main.cpp ikigai_math basics)
ikigai_register_test(utils_small_vector_lifetime utils_module small_vector_lifetime)
ikigai_register_test(utils_expression_compilation utils_module expression_compilation)
target_sources(utils_module PRIVATE
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/stringUtils.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/expressions/expression.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/utilsModule/variables/entityRef.cpp")
target_link_libraries(utils_module PRIVATE ikigai_json_serde)
ikigai_add_module_test(physics2d_module unit/physics2d/main.cpp ikigai_physics2d basics)
ikigai_register_test(physics2d_distance_joint physics2d_module distance_joint)
ikigai_add_module_test(physics_module unit/physics/main.cpp ikigai_physics3d basics)
ikigai_register_test(physics_hinge physics_module hinge)

ikigai_add_module_test(scene_document unit/scene/main.cpp ikigai_json_serde hierarchy)
target_include_directories(scene_document PRIVATE "${IKIGAI_SOURCE_ROOT}/engine")
target_sources(scene_document PRIVATE "${IKIGAI_SOURCE_ROOT}/engine/sceneModule/sceneDocument.cpp")
ikigai_register_test(scene_prefab_expansion scene_document prefab_expansion)

include("${ENGINE_ROOT}/cmake/RenderCommands.cmake")
ikigai_add_module_test(render_graph_compiler unit/renderGraph/main.cpp ikigai_render_commands schedule)
ikigai_register_test(render_graph_validation render_graph_compiler validation)

ikigai_add_module_test(vfs_mounts unit/vfs/main.cpp ikigai_coroutines)
target_sources(vfs_mounts PRIVATE
	"${IKIGAI_SOURCE_ROOT}/engine/resourceModule/fileSystem/fileSystem.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/resourceModule/fileSystem/storage.cpp"
	"${IKIGAI_SOURCE_ROOT}/engine/resourceModule/fileSystem/userStorage.cpp")
target_include_directories(vfs_mounts SYSTEM PRIVATE
	"${ENGINE_ROOT}/3rd/vfspp" # ZipFileSystem.hpp includes its vendor header relative to this directory.
	"${ENGINE_ROOT}/3rd/vfspp/include"
	"${ENGINE_ROOT}/3rd/vfspp/vendor/miniz-cpp")
target_link_libraries(vfs_mounts PRIVATE ikigai_services ikigai_task)

option(IKIGAI_BUILD_ECS_BENCHMARKS "Build comparative ECS benchmarks from local dependency checkouts" OFF)
if(IKIGAI_BUILD_ECS_BENCHMARKS)
    add_subdirectory(benchmarks/ecs)
endif()

if(WIN32)
    add_subdirectory(tools/crashReporter)
endif()
