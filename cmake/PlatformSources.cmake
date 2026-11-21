# Selects engine backend/platform translation units from explicit source sets.
# Third-party sources and platform bootstrap libraries remain owned by their projects.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SourceSelection.cmake")

function(ikigai_filter_platform_sources headers_var sources_var platform)
    # Backend lists are the same manifests used to create the backend targets.
    set(quest_only
        "engine/resourceModule/fileSystem/androidAssetFileSystem.cpp"
        "engine/resourceModule/fileSystem/androidAssetFileSystem.h"
        "engine/windowModule/window/oxrInput.cpp"
        "engine/windowModule/window/oxrInput.h"
        "platform/quest/android_main.cpp")

    set(quest_excluded
        "engine/resourceModule/fileSystem/sdlFileSystem.cpp"
        # The Quest runtime has no ImGui view host.
        "engine/renderModule/backends/gl/imguiBackendGl.cpp"
        "engine/renderModule/backends/imgui/imguiBackend.cpp"
        "engine/renderModule/backends/imgui/imguiBackend.h"
        "engine/imguiViewsModule/viewRenderer.cpp"
        "main.cpp")

    set(excluded)
    if(NOT USE_OPENGL AND NOT platform STREQUAL "android" AND NOT platform STREQUAL "quest")
        list(APPEND excluded ${IKIGAI_MANIFEST_renderGl})
    endif()
    if(platform STREQUAL "android" OR platform STREQUAL "quest" OR platform STREQUAL "web" OR NOT USE_VULKAN)
        list(APPEND excluded ${IKIGAI_MANIFEST_renderVk})
    endif()
    if(NOT platform STREQUAL "win" OR NOT USE_DX12)
        list(APPEND excluded ${IKIGAI_MANIFEST_renderDx12})
    endif()
    if(NOT (platform STREQUAL "mac" OR platform STREQUAL "ios") OR NOT USE_METAL)
        list(APPEND excluded ${IKIGAI_MANIFEST_renderMetal})
    endif()
    if(NOT platform STREQUAL "quest")
        list(APPEND excluded ${quest_only})
    endif()
    if(platform STREQUAL "win" AND (NOT USE_GPU_DRIVEN OR NOT (USE_VULKAN OR USE_DX12)))
        list(APPEND excluded "engine/renderModule/gpuScene.cpp")
    endif()
    if(platform STREQUAL "quest")
        list(APPEND excluded ${quest_excluded} ${IKIGAI_MANIFEST_editorModule})
    endif()
    ikigai_exclude_sources(${headers_var} ${excluded})
    ikigai_exclude_sources(${sources_var} ${excluded})
    set(${headers_var} "${${headers_var}}" PARENT_SCOPE)
    set(${sources_var} "${${sources_var}}" PARENT_SCOPE)
endfunction()
