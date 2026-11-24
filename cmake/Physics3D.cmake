# Builds the 3D physics library and configures its application integration.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SourceSelection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PhysicsReflection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Collision.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/JsonSerde.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/StopToken.cmake")
option(USE_PHYSICS_3D "Enable 3D rigid/soft/cloth/fluid physics" ON)
get_filename_component(IKIGAI_PHYSICS_3D_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
function(ikigai_add_physics3d_library)
    if(TARGET ikigai_physics3d)
        return()
    endif()
    set(root "${IKIGAI_PHYSICS_3D_ROOT}")
    ikigai_add_collision_library(3)
    add_library(ikigai_earcut INTERFACE)
    target_include_directories(ikigai_earcut SYSTEM INTERFACE "${root}/3rd/earcut-hpp/include")
    add_library(ikigai_physics3d STATIC
        "${root}/src/engine/physicsModule/body.cpp"
        "${root}/src/engine/physicsModule/contacts.cpp"
        "${root}/src/engine/physicsModule/characterQueries.cpp"
        "${root}/src/engine/physicsModule/narrowPhase.cpp"
        "${root}/src/engine/physicsModule/physicWorld.cpp"
        "${root}/src/engine/physicsModule/rigidJoint.cpp"
        "${root}/src/engine/physicsModule/rigidJointSolver.cpp"
        "${root}/src/engine/physicsModule/particles.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/planeSlicer.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/concaveDestruction.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/supportGraph.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/supportAnchors.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/fracturePreparation.cpp"
        "${root}/src/engine/destructionModule/destructionRuntime.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/destructibleDefinition.cpp"
        "${root}/src/engine/destructionModule/destructibleInstance.cpp")
    target_compile_features(ikigai_physics3d PUBLIC cxx_std_20)
    target_link_libraries(ikigai_physics3d PUBLIC ikigai_collision3d ikigai_json_serde PRIVATE ikigai_earcut)
    ikigai_enable_stop_token(ikigai_physics3d)
    target_include_directories(ikigai_physics3d PUBLIC "${root}/src/engine")
    set_target_properties(ikigai_physics3d PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(ikigai_physics3d PRIVATE /W4 /utf-8)
    else()
        target_compile_options(ikigai_physics3d PRIVATE -Wall -Wextra)
    endif()
endfunction()

function(ikigai_configure_physics3d target)
    target_compile_definitions(${target} PRIVATE USE_PHYSICS_3D=$<BOOL:${USE_PHYSICS_3D}>)
    get_target_property(sources ${target} SOURCES)
    ikigai_exclude_sources(sources
        "engine/collisionModule/closedSurfaceMesh.cpp"
        "engine/collisionModule/collision2d.cpp"
        "engine/collisionModule/collision3d.cpp"
        "engine/collisionModule/compoundCollider.cpp"
        "engine/collisionModule/convexCollision.cpp"
        "engine/collisionModule/convexPolyhedron.cpp")
    ikigai_exclude_sources(sources
        "engine/destructionModule/concaveDestruction.cpp"
        "engine/destructionModule/destructibleDefinition.cpp"
        "engine/destructionModule/destructibleInstance.cpp"
        "engine/destructionModule/destructionRuntime.cpp"
        "engine/destructionModule/fracturePreparation.cpp"
        "engine/destructionModule/planeSlicer.cpp"
        "engine/destructionModule/supportAnchors.cpp"
        "engine/destructionModule/supportGraph.cpp")
    ikigai_exclude_sources(sources
        "engine/physicsModule/body.cpp"
        "engine/physicsModule/characterQueries.cpp"
        "engine/physicsModule/contacts.cpp"
        "engine/physicsModule/narrowPhase.cpp"
        "engine/physicsModule/particles.cpp"
        "engine/physicsModule/physicWorld.cpp"
        "engine/physicsModule/rigidJoint.cpp"
        "engine/physicsModule/rigidJointSolver.cpp")
    if(NOT USE_PHYSICS_3D)
        ikigai_exclude_sources(sources
            "engine/coreModule/ecs/components/destructibleMeshComponent.cpp"
            "engine/coreModule/ecs/systems/destructionSystem.cpp"
            "engine/resourceModule/destructibleManager.cpp")
        ikigai_exclude_sources(sources
            "engine/coreModule/ecs/components/ragdollComponent.cpp"
            "engine/coreModule/ecs/systems/ragdollSystem.cpp"
            "engine/resourceModule/ragdollManager.cpp"
            "engine/ragdollModule/ragdollDefinition.cpp"
            "engine/ragdollModule/ragdollDefinition.h"
            "engine/ragdollModule/ragdollInstance.cpp"
            "engine/ragdollModule/ragdollInstance.h")
        ikigai_exclude_sources(sources
            "engine/physicsModule/body.cpp"
            "engine/physicsModule/characterQueries.cpp"
            "engine/physicsModule/contacts.cpp"
            "engine/physicsModule/forceGenerator.cpp"
            "engine/physicsModule/joint.cpp"
            "engine/physicsModule/narrowPhase.cpp"
            "engine/physicsModule/particles.cpp"
            "engine/physicsModule/physicWorld.cpp"
            "engine/physicsModule/rigidJoint.cpp"
            "engine/physicsModule/rigidJointSolver.cpp")
        ikigai_exclude_sources(sources
            "engine/coreModule/ecs/components/physicsGrabComponent.cpp"
            "engine/coreModule/ecs/components/physics3dComponent.cpp"
            "engine/coreModule/ecs/components/physicsComponent.cpp"
            "engine/coreModule/ecs/systems/physics3dSystem.cpp"
            "engine/renderModule/physics/deformableRenderer.cpp")
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    if(USE_PHYSICS_3D)
        ikigai_add_physics3d_library()
        target_link_libraries(${target} PRIVATE ikigai_physics3d)
        if(MSVC)
            target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:/PDBPAGESIZE:8192>)
        endif()
    endif()
endfunction()
