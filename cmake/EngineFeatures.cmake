include_guard(GLOBAL)

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

# Included after the engine source glob. list(FILTER) runs in that scope.
foreach(ikigai_source_list PROJECT_ABSOLUTE_SOURCES PROJECT_SOURCES)
    list(FILTER ${ikigai_source_list} EXCLUDE REGEX "(^|[/\\\\])EcsDemo\\.cpp$")
    if(USE_RAY_TRACING)
        list(FILTER ${ikigai_source_list} EXCLUDE REGEX "(^|[/\\\\])gameRayTracingStub\\.cpp$")
    else()
        list(FILTER ${ikigai_source_list} EXCLUDE REGEX "(^|[/\\\\])gameRayTracing\\.cpp$")
        list(FILTER ${ikigai_source_list} EXCLUDE REGEX "(^|[/\\\\])rayTracingVk\\.cpp$")
        list(FILTER ${ikigai_source_list} EXCLUDE REGEX "(^|[/\\\\])rayTracingDx12\\.cpp$")
    endif()
endforeach()

function(ikigai_apply_feature_definitions target)
    target_compile_definitions(${target} PRIVATE
        USE_CHEATS=$<IF:$<CONFIG:Debug>,1,0>
        USE_RAY_TRACING=$<BOOL:${USE_RAY_TRACING}>)
endfunction()
