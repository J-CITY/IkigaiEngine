# Builds the windows application and its platform dependencies.
#TODO 
# soloud сборка


cmake_minimum_required(VERSION 3.20)

set(APP_NAME "IkigaiEngine")
project(${APP_NAME} LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../Python.cmake")
include(CMakeDependentOption)

# --- Configuration Options ---
include("${CMAKE_CURRENT_LIST_DIR}/../EngineOptions.cmake")
ikigai_configure_engine_options(win)
option(USE_GPU_DRIVEN "Build GPU scene submission for ordinary meshes" ON)


# --- Directory Setup ---
get_filename_component(IKIGAI_PLATFORM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(THIRD_PARTY_DIR "${IKIGAI_PLATFORM_ROOT}/3rd")
set(MAIN_SOURCE_DIR "${IKIGAI_PLATFORM_ROOT}/src")
include("${CMAKE_CURRENT_LIST_DIR}/../OutputDirectories.cmake")
ikigai_configure_output_directories(windows)

# --- Dependencies ---

# 1. Assimp
add_subdirectory(${THIRD_PARTY_DIR}/assimp ./3rd/assimp/build)

# 2. Freetype
add_subdirectory(${THIRD_PARTY_DIR}/freetype ./3rd/freetype/build)

# 3. GLSLang
option(ENABLE_HLSL OFF)
option(SKIP_GLSLANG_INSTALL ON)
option(ENABLE_CTEST OFF)
option(ENABLE_GLSLANG_BINARIES OFF)
option(ENABLE_SPVREMAPPER OFF)
# option(ENABLE_OPT OFF) # Uncomment if needed
add_subdirectory(${THIRD_PARTY_DIR}/glslang ./3rd/glslang/build)

# 4. SPIRV-Cross
set(SPIRV_CROSS_SOURCE "${THIRD_PARTY_DIR}/SPIRV-Cross")
if(USE_DX12)
    include("${CMAKE_CURRENT_LIST_DIR}/../SpirvCross.cmake")
    ikigai_prepare_spirv_cross("${SPIRV_CROSS_SOURCE}" SPIRV_CROSS_SOURCE)
endif()
option(SPIRV_CROSS_SKIP_INSTALL ON)
option(SPIRV_CROSS_CLI OFF)
option(SPIRV_CROSS_ENABLE_TESTS OFF)
option(SPIRV_CROSS_ENABLE_C_API OFF)
option(SPIRV_CROSS_ENABLE_CPP OFF)
option(SPIRV_CROSS_ENABLE_UTIL OFF)
add_subdirectory("${SPIRV_CROSS_SOURCE}" ./3rd/SPIRV-Cross/build)

# 5. SPIRV-Reflect
file(GLOB SPIRV_REFLECT_SRC 
    "${THIRD_PARTY_DIR}/SPIRV-Reflect/spirv_reflect.cpp" 
    "${THIRD_PARTY_DIR}/SPIRV-Reflect/spirv_reflect.h"
)
add_library(spirv-reflect STATIC ${SPIRV_REFLECT_SRC})
target_include_directories(spirv-reflect PUBLIC "${THIRD_PARTY_DIR}/SPIRV-Reflect")
set_property(TARGET spirv-reflect PROPERTY FOLDER "3rd/SPIRV-Reflect")

# 5b. Optional offline virtual mesh cooker
option(IKIGAI_ENABLE_VG_COOKER "Build the Windows virtual mesh cooker command" ON)
if(IKIGAI_ENABLE_VG_COOKER)
    include("${CMAKE_CURRENT_LIST_DIR}/../VMeshCooker.cmake")
endif()

# 6. ImGUI
set(IMGUI_DIR "${THIRD_PARTY_DIR}/imgui")
file(GLOB IMGUI_SOURCES 
    "${IMGUI_DIR}/imgui/*.cpp"
    "${IMGUI_DIR}/imgui/*.h"
    "${IMGUI_DIR}/imgui/misc/cpp/*.cpp"
    "${IMGUI_DIR}/imgui/misc/cpp/*.h"
    "${IMGUI_DIR}/IconFont/*.h"
    # Widgets
    "${IMGUI_DIR}/imgui/GraphEditor.cpp" "${IMGUI_DIR}/imgui/GraphEditor.h" 
    "${IMGUI_DIR}/imgui/ImCurveEdit.cpp" "${IMGUI_DIR}/imgui/ImCurveEdit.h"
    "${IMGUI_DIR}/imgui/ImGradient.cpp" "${IMGUI_DIR}/imgui/ImGradient.h"
    "${IMGUI_DIR}/imgui/ImZoomSlider.h"
)

if(USE_SDL)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_sdl3.cpp" 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_sdl3.h"
    )
endif()

if(USE_OPENGL)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_opengl3.cpp" 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_opengl3.h"
        "${IMGUI_DIR}/imgui/backends/imgui_impl_opengl3_loader.h"
    )
endif()

if(USE_VULKAN)
    include("${CMAKE_CURRENT_LIST_DIR}/../ImGuiVulkan.cmake")
    ikigai_prepare_imgui_vulkan("${IMGUI_DIR}/imgui/backends" imgui_vulkan_source)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_vulkan.h"
        "${imgui_vulkan_source}"
    )
endif()

if(USE_DX12)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_dx12.h"
        "${IMGUI_DIR}/imgui/backends/imgui_impl_dx12.cpp"
    )
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC 
    "${IMGUI_DIR}/imgui" 
    "${IMGUI_DIR}/IconFont"
)

if(USE_SDL)
    target_include_directories(imgui PRIVATE "${THIRD_PARTY_DIR}/SDL3/include")
endif()

if(USE_VULKAN)
    target_include_directories(imgui PRIVATE "${THIRD_PARTY_DIR}/Vulkan-Headers/include")
    target_compile_definitions(imgui PRIVATE VK_NO_PROTOTYPES)
endif()

# 7. Spine
file(GLOB_RECURSE SPINE_FILES "${THIRD_PARTY_DIR}/spine/spine/*.cpp" "${THIRD_PARTY_DIR}/spine/spine/*.h")
add_library(spine STATIC ${SPINE_FILES})
target_include_directories(spine PUBLIC "${THIRD_PARTY_DIR}/spine")
target_compile_definitions(spine PUBLIC SPINE_USE_STD_FUNCTION)

# 8. Lua
include("${CMAKE_CURRENT_LIST_DIR}/../Scripting.cmake")
if(USE_LUA)
file(GLOB_RECURSE LUA_FILES "${THIRD_PARTY_DIR}/lua/*.c")
list(FILTER LUA_FILES EXCLUDE REGEX "lua\\.c$")
list(FILTER LUA_FILES EXCLUDE REGEX "luac\\.c$")
add_library(lua STATIC ${LUA_FILES})
target_include_directories(lua PUBLIC "${THIRD_PARTY_DIR}/lua")
endif()

# 9. Volk
add_library(volk STATIC "${THIRD_PARTY_DIR}/volk/volk.c" "${THIRD_PARTY_DIR}/volk/volk.h")
target_include_directories(volk PUBLIC "${THIRD_PARTY_DIR}/Vulkan-Headers/include")
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

# 10. SDL3
if(USE_SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    if(USE_DLSS)
        # NGX uses the dynamic CRT; SDL's libc-less memmove conflicts with it.
        set(SDL_LIBC ON CACHE BOOL "Use system CRT with NGX" FORCE)
    endif()
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY_DIR}/SDL3 ./3rd/SDL3/build)
endif()

# 11. SoLoud (miniaudio, no SDL)
file(GLOB_RECURSE SOLOUD_SOURCES
    "${THIRD_PARTY_DIR}/soloud/src/audiosource/*.cpp"
    "${THIRD_PARTY_DIR}/soloud/src/audiosource/*.c"
    "${THIRD_PARTY_DIR}/soloud/src/core/*.cpp"
    "${THIRD_PARTY_DIR}/soloud/src/filter/*.cpp"
)
list(APPEND SOLOUD_SOURCES "${THIRD_PARTY_DIR}/soloud/src/backend/miniaudio/soloud_miniaudio.cpp")
add_library(soloud STATIC ${SOLOUD_SOURCES})
target_include_directories(soloud PUBLIC "${THIRD_PARTY_DIR}/soloud/include")
target_compile_definitions(soloud PUBLIC WITH_MINIAUDIO)
target_compile_definitions(soloud PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# --- Main Application ---

# Sources
include("${CMAKE_CURRENT_LIST_DIR}/../EngineSources.cmake")
ikigai_collect_engine_sources("${MAIN_SOURCE_DIR}" PROJECT_ABSOLUTE_HEADERS PROJECT_ABSOLUTE_SOURCES)

include("${CMAKE_CURRENT_LIST_DIR}/../PlatformSources.cmake")
ikigai_filter_platform_sources(PROJECT_ABSOLUTE_HEADERS PROJECT_ABSOLUTE_SOURCES win)

# Source Groups for IDE
source_group(TREE ${MAIN_SOURCE_DIR} FILES ${PROJECT_ABSOLUTE_HEADERS} ${PROJECT_ABSOLUTE_SOURCES})

set(ENGINE_WIN_SUBSYSTEM "windows" CACHE STRING "Windows executable subsystem: windows or console")

if(WIN32)
    set(EXECUTABLE_FLAG "")
    if(ENGINE_WIN_SUBSYSTEM STREQUAL "windows")
        set(EXECUTABLE_FLAG WIN32)
    elseif(NOT ENGINE_WIN_SUBSYSTEM STREQUAL "console")
        message(FATAL_ERROR "ENGINE_WIN_SUBSYSTEM must be 'windows' or 'console'")
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../VectorGraphics.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../EngineCore.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../EngineFeatures.cmake")

add_executable(${APP_NAME} ${EXECUTABLE_FLAG}
    ${PROJECT_ABSOLUTE_HEADERS}
    ${PROJECT_ABSOLUTE_SOURCES}
)

# --- Generate Reflection Headers ---
include("${CMAKE_CURRENT_LIST_DIR}/../Codegen.cmake")
ikigai_configure_reflection(${APP_NAME})

# Properties
set_target_properties(${APP_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(${APP_NAME} PRIVATE /bigobj /MP12 /Zm200)

# Definitions
target_compile_definitions(${APP_NAME} PRIVATE NOMINMAX)
if(USE_GPU_DRIVEN AND (USE_VULKAN OR USE_DX12))
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_USE_GPU_DRIVEN)
endif()
target_compile_definitions(${APP_NAME} PRIVATE SOL_EXCEPTIONS_SAFE_PROPAGATION=1 SOL_NO_LUA_HPP=1)
target_compile_definitions(${APP_NAME} PRIVATE $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>)

if(USE_SDL)
    target_compile_definitions(${APP_NAME} PRIVATE USE_SDL)
endif()

if(USE_OPENGL)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_OPENGL OPENGL_BACKEND)
endif()

if(USE_VULKAN)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_VULKAN VULKAN_BACKEND)
endif()

if(USE_DX12)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_DX12 DX12_BACKEND)
endif()

# Additional definitions
ikigai_apply_feature_definitions(${APP_NAME})
if(IKIGAI_ENABLE_VG_COOKER)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_VG_COOKER)
    target_link_libraries(${APP_NAME} PRIVATE ikigai_vmesh_cooker)
endif()
if(USE_EDITOR)
    target_compile_definitions(${APP_NAME} PRIVATE USE_EDITOR)
endif()
if(USE_EDITOR AND USE_FILE_WATCHER)
    target_compile_definitions(${APP_NAME} PRIVATE USE_FILE_WATCHER)
endif()

# --- Includes and Linking ---

target_include_directories(${APP_NAME} PRIVATE
    ${MAIN_SOURCE_DIR}/engine
    ${THIRD_PARTY_DIR}/serdepp/include
    ${THIRD_PARTY_DIR}/magic_enum/include
    ${THIRD_PARTY_DIR}/magic_enum/include/magic_enum
    ${THIRD_PARTY_DIR}/nameof/include
    ${THIRD_PARTY_DIR}/json/single_include
    ${THIRD_PARTY_DIR}/soloud/include
    ${THIRD_PARTY_DIR}/fmt/include
    ${THIRD_PARTY_DIR}/vfspp/include
    ${THIRD_PARTY_DIR}/vfspp/vendor/miniz-cpp

    ${THIRD_PARTY_DIR}/volk
    ${THIRD_PARTY_DIR}/Vulkan-Headers/include
    ${THIRD_PARTY_DIR}/VMA/include
    ${THIRD_PARTY_DIR}/D3D12MA/src
)

set(LIBS_TO_LINK
    imgui
    spine

    assimp
    freetype
    soloud
    SPIRV
    glslang
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-hlsl
    spirv-cross-msl
    spirv-cross-reflect
    spirv-reflect
)

if(USE_OPENGL)
    target_link_libraries(${APP_NAME} PRIVATE opengl32)
    target_link_libraries(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/glew/lib/Release/x64/glew32s.lib")
    target_include_directories(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/glew/include")
endif()

if(USE_VULKAN)
    target_link_libraries(${APP_NAME} PRIVATE volk)
endif()

if(USE_DX12)
    target_link_libraries(${APP_NAME} PRIVATE d3d12.lib d3dcompiler.lib dxcompiler.lib)
    target_sources(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/D3D12MA/src/D3D12MemAlloc.cpp")
    target_include_directories(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/D3D12MA/include")
endif()

if(USE_SDL)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(${APP_NAME} PRIVATE SDL3::SDL3main)
    endif()
    target_link_libraries(${APP_NAME} PRIVATE SDL3::SDL3-static)
    target_include_directories(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/SDL3/include")
endif()

target_link_libraries(${APP_NAME} PRIVATE ${LIBS_TO_LINK})
include("${CMAKE_CURRENT_LIST_DIR}/../Upscalers.cmake")

# Some bundled dependencies cache their output directory. Keep alternate backend
# builds runnable when ENGINE_RUNTIME_OUTPUT_DIRECTORY points at another folder.
foreach(runtime_target assimp freetype glslang SPIRV glslang-default-resource-limits SPIRV-Tools-shared)
    if(TARGET ${runtime_target})
        get_target_property(runtime_type ${runtime_target} TYPE)
        if(runtime_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET ${APP_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${runtime_target}> $<TARGET_FILE_DIR:${APP_NAME}>
                VERBATIM)
        endif()
    endif()
endforeach()

set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT /NODEFAULTLIB:LIBCMTD /ignore:4099")

# Independent vector geometry and SDF font module
target_link_libraries(${APP_NAME} PRIVATE ikigai_vector)
if(IKIGAI_VECTOR_TESTS)
    add_executable(ikigai_vector_components_tests "${IKIGAI_PLATFORM_ROOT}/utils/tests/vector_components.cpp")
    get_target_property(vector_engine_includes ${APP_NAME} INCLUDE_DIRECTORIES)
    target_include_directories(ikigai_vector_components_tests PRIVATE ${vector_engine_includes})
    target_compile_features(ikigai_vector_components_tests PRIVATE cxx_std_20)
    target_compile_definitions(ikigai_vector_components_tests PRIVATE NOMINMAX)
    target_compile_options(ikigai_vector_components_tests PRIVATE /bigobj)
    add_test(NAME vector_components COMMAND ikigai_vector_components_tests)
endif()

ikigai_configure_scripting(${APP_NAME})
if(COMMAND ikigai_add_scripting_tests)
    ikigai_add_scripting_tests(${APP_NAME})
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../Networking.cmake")
ikigai_configure_network(${APP_NAME})

# Shared application startup and optional local diagnostics.
include("${CMAKE_CURRENT_LIST_DIR}/../Application.cmake")
ikigai_configure_application(${APP_NAME})

# Link independent libraries and build the remaining shared module targets.
ikigai_link_engine_core(${APP_NAME})
