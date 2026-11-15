# Builds the 2D physics library and configures its application integration.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SourceSelection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PhysicsReflection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Collision.cmake")
option(USE_PHYSICS_2D "Enable educational 2D rigid/soft/fluid physics" OFF)
option(IKIGAI_PHYSICS_2D_TESTS "Build independent 2D physics regression tests" OFF)
get_filename_component(IKIGAI_PHYSICS_2D_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(ikigai_configure_physics2d target)
    target_compile_definitions(${target} PRIVATE USE_PHYSICS_2D=$<BOOL:${USE_PHYSICS_2D}>)
    get_target_property(sources ${target} SOURCES)
    # Kernel translation units belong to their libraries, never the application.
    ikigai_exclude_sources(sources
        "engine/physics2dModule/collision.cpp"
        "engine/physics2dModule/particles.cpp"
        "engine/physics2dModule/world2d.cpp")
    ikigai_exclude_sources(sources
        "engine/collisionModule/closedSurfaceMesh.cpp"
        "engine/collisionModule/collision2d.cpp"
        "engine/collisionModule/collision3d.cpp"
        "engine/collisionModule/compoundCollider.cpp"
        "engine/collisionModule/convexCollision.cpp"
        "engine/collisionModule/convexPolyhedron.cpp")
    if(NOT USE_PHYSICS_2D)
        ikigai_exclude_sources(sources
            "engine/coreModule/ecs/components/cloth2dComponent.cpp"
            "engine/coreModule/ecs/components/forceField2dComponent.cpp"
            "engine/coreModule/ecs/components/physics2dComponent.cpp"
            "engine/coreModule/ecs/systems/physics2dSystem.cpp"
            "engine/generated/cloth2dComponent.generated.cpp"
            "engine/generated/forceField2dComponent.generated.cpp"
            "engine/generated/physics2dComponent.generated.cpp"
            "${CMAKE_BINARY_DIR}/generated/cloth2dComponent.generated.cpp"
            "${CMAKE_BINARY_DIR}/generated/forceField2dComponent.generated.cpp"
            "${CMAKE_BINARY_DIR}/generated/physics2dComponent.generated.cpp")
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    if(USE_PHYSICS_2D)
        ikigai_add_physics2d_library()
        target_link_libraries(${target} PRIVATE ikigai_physics2d)
        if(MSVC)
            # The engine's generated reflection can exceed the 4 GiB PDB limit
            # with default 4 KiB pages. 8 KiB pages allow a larger Debug database.
            target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:/PDBPAGESIZE:8192>)
        endif()
    endif()
    if(IKIGAI_PHYSICS_2D_TESTS)
        ikigai_configure_physics2d_tests(${target})
    endif()
endfunction()

function(ikigai_add_physics2d_library)
    if(TARGET ikigai_physics2d)
        return()
    endif()
    set(root "${IKIGAI_PHYSICS_2D_ROOT}")
    ikigai_add_collision_library(2)
    add_library(ikigai_physics2d STATIC
        "${root}/src/engine/physics2dModule/collision.cpp"
        "${root}/src/engine/physics2dModule/world2d.cpp"
        "${root}/src/engine/physics2dModule/particles.cpp")
    target_include_directories(ikigai_physics2d PUBLIC "${root}/src/engine")
    target_link_libraries(ikigai_physics2d PUBLIC ikigai_collision2d)
    target_compile_features(ikigai_physics2d PUBLIC cxx_std_20)
    set_target_properties(ikigai_physics2d PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(ikigai_physics2d PRIVATE /W4 /utf-8)
    else()
        target_compile_options(ikigai_physics2d PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(IKIGAI_PHYSICS_2D_TESTS)
    include("${CMAKE_CURRENT_LIST_DIR}/../utils/tests/cmake/Physics2D.cmake" OPTIONAL)
endif()
