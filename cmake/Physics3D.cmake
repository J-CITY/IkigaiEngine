include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/PhysicsReflection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Collision.cmake")
option(USE_PHYSICS_3D "Enable 3D rigid/soft/cloth/fluid physics" ON)
get_filename_component(IKIGAI_PHYSICS_3D_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
function(ikigai_add_physics3d_library)
    if(TARGET ikigai_physics3d)
        return()
    endif()
    set(root "${IKIGAI_PHYSICS_3D_ROOT}")
    ikigai_add_collision_library(3)
    add_library(ikigai_physics3d STATIC
        "${root}/src/engine/physicsModule/body.cpp"
        "${root}/src/engine/physicsModule/contacts.cpp"
        "${root}/src/engine/physicsModule/narrowPhase.cpp"
        "${root}/src/engine/physicsModule/physicWorld.cpp"
        "${root}/src/engine/physicsModule/rigidJoint.cpp"
        "${root}/src/engine/physicsModule/rigidJointSolver.cpp"
        "${root}/src/engine/physicsModule/particles.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/planeSlicer.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/fracturePreparation.cpp"
        "${root}/src/engine/destructionModule/destructionRuntime.cpp")
    target_sources(ikigai_physics3d PRIVATE "${root}/src/engine/destructionModule/destructibleDefinition.cpp"
        "${root}/src/engine/destructionModule/destructibleInstance.cpp")
    target_compile_features(ikigai_physics3d PUBLIC cxx_std_20)
    target_link_libraries(ikigai_physics3d PUBLIC ikigai_collision3d)
    target_include_directories(ikigai_physics3d PUBLIC "${root}/src/engine"
        "${root}/3rd/serdepp/include" "${root}/3rd/json/include" "${root}/3rd/nameof/include"
        "${root}/3rd/magic_enum/include/magic_enum")
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
    list(FILTER sources EXCLUDE REGEX "collisionModule/.*\\.cpp$")
    list(FILTER sources EXCLUDE REGEX "destructionModule/.*\\.cpp$")
    list(FILTER sources EXCLUDE REGEX "physicsModule/(body|contacts|narrowPhase|physicWorld|particles|rigidJoint|rigidJointSolver)\\.cpp$")
    list(FILTER sources EXCLUDE REGEX "(physics3dComponent|physicsComponent)\\.generated\\.cpp$")
    if(NOT USE_PHYSICS_3D)
        list(FILTER sources EXCLUDE REGEX "(destructibleMeshComponent|destructionSystem|destructibleManager)\\.cpp$")
        list(FILTER sources EXCLUDE REGEX "(ragdollComponent|ragdollSystem|ragdollDefinition|ragdollInstance|ragdollManager)\\.cpp$")
        list(FILTER sources EXCLUDE REGEX "physicsModule/.*\\.cpp$")
        list(FILTER sources EXCLUDE REGEX "(physics3dComponent|physicsComponent|physics3dSystem|deformableRenderer)\\.cpp$")
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    if(USE_PHYSICS_3D)
        ikigai_add_physics3d_library()
        target_link_libraries(${target} PRIVATE ikigai_physics3d)
        if(MSVC)
            target_link_options(${target} PRIVATE $<$<CONFIG:Debug>:/PDBPAGESIZE:8192>)
        endif()
        ikigai_add_physics_reflection()
        add_dependencies(${target} ikigai_physics_reflection)
        foreach(name physics3dComponent physicsComponent)
            target_sources(${target} PRIVATE "${IKIGAI_PHYSICS_3D_ROOT}/src/engine/generated/${name}.generated.cpp")
        endforeach()
    endif()
endfunction()
