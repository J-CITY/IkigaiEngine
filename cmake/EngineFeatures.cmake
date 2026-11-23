include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SourceSelection.cmake")

# Ray tracing stays in the engine target. The flag only chooses which
# translation units are compiled. Android, Quest and Emscripten have no
# Vulkan/DX12 ray-tracing backend in this project.
if(ANDROID OR EMSCRIPTEN)
    set(_IKIGAI_RAY_TRACING_DEFAULT OFF)
else()
    set(_IKIGAI_RAY_TRACING_DEFAULT ON)
endif()
option(USE_RAY_TRACING "Compile hardware ray tracing" ${_IKIGAI_RAY_TRACING_DEFAULT})
if(USE_RAY_TRACING AND (ANDROID OR EMSCRIPTEN))
    message(FATAL_ERROR "USE_RAY_TRACING cannot be enabled on Android, Quest, or Emscripten")
endif()

# Select explicit implementation alternatives in the application source lists.
foreach(ikigai_source_list PROJECT_ABSOLUTE_SOURCES PROJECT_SOURCES)
    ikigai_exclude_sources(${ikigai_source_list}
        "EcsDemo.cpp")
    # These modules stay out of the link. Call sites compile against header stubs
    # (capture, profiler) or are omitted with the ECS glue (AI).
    if(NOT USE_AI)
        ikigai_exclude_sources(${ikigai_source_list} ${IKIGAI_MANIFEST_aiModule}
            "engine/coreModule/ecs/characterBehaviorHost.cpp"
            "engine/coreModule/ecs/components/behaviorTreeComponent.cpp"
            "engine/coreModule/ecs/components/blackboardComponent.cpp"
            "engine/coreModule/ecs/systems/behaviorTreeSystem.cpp"
            "engine/resourceModule/behaviorTreeManager.cpp"
            "engine/resourceModule/blackboardManager.cpp")
    endif()
    if(NOT USE_CAPTURE)
        ikigai_exclude_sources(${ikigai_source_list} ${IKIGAI_MANIFEST_captureModule})
    endif()
    if(NOT USE_PROFILER)
        ikigai_exclude_sources(${ikigai_source_list} ${IKIGAI_MANIFEST_profilerModule})
    endif()
    if(USE_RAY_TRACING)
        ikigai_exclude_sources(${ikigai_source_list}
            "engine/renderModule/gameRayTracingStub.cpp")
    else()
        ikigai_exclude_sources(${ikigai_source_list}
            "engine/renderModule/gameRayTracing.cpp")
        ikigai_exclude_sources(${ikigai_source_list}
            "engine/renderModule/backends/vk/rayTracingVk.cpp")
        ikigai_exclude_sources(${ikigai_source_list}
            "engine/renderModule/backends/dx12/rayTracingDx12.cpp")
    endif()
endforeach()

function(ikigai_apply_feature_definitions target)
    target_compile_definitions(${target} PRIVATE
        USE_CHEATS=$<IF:$<CONFIG:Debug>,1,0>
        USE_RAY_TRACING=$<BOOL:${USE_RAY_TRACING}>
        USE_AI=$<BOOL:${USE_AI}>
        USE_CAPTURE=$<BOOL:${USE_CAPTURE}>
        USE_PROFILER=$<BOOL:${USE_PROFILER}>)
endfunction()
