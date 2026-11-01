#TODO 
# soloud сборка


cmake_minimum_required(VERSION 3.20)

set(APP_NAME "IkigaiEngine")
project(${APP_NAME} LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Python.cmake")
include(CMakeDependentOption)

# --- Configuration Options ---
option(USE_SDL      "Use SDL window backend" ON)
option(USE_OPENGL   "Use OpenGL rendering backend" ON)
option(USE_VULKAN   "Use Vulkan rendering backend" OFF)
option(USE_DX12     "Use DX12 rendering backend" OFF)
option(USE_DLSS     "Enable NVIDIA DLSS Super Resolution" OFF)
option(USE_FSR      "Enable AMD FSR Upscaling" OFF)
option(USE_METAL    "Use Metal rendering backend" OFF)
option(USE_MIMALLOC "Use mimalloc override" OFF)
option(USE_EDITOR "Build with in-engine editor UI" ON)
cmake_dependent_option(USE_FILE_WATCHER "Watch asset files for hot reload" ON "USE_EDITOR" OFF)

if(NOT USE_OPENGL AND NOT USE_VULKAN AND NOT USE_DX12)
    message(FATAL_ERROR "At least one render backend must be enabled (USE_OPENGL, USE_VULKAN, USE_DX12)")
endif()

# --- Directory Setup ---
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd")
set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(ENGINE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out" CACHE PATH "Directory for the built executable")
if(NOT IS_ABSOLUTE "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}")
    get_filename_component(ENGINE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_RUNTIME_OUTPUT_DIRECTORY}" ABSOLUTE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}")

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
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SpirvCross.cmake")
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

# 5b. meshoptimizer (virtual geometry cooker: clustering, simplification, cluster LOD)
file(GLOB MESHOPTIMIZER_SRC "${THIRD_PARTY_DIR}/meshoptimizer/src/*.cpp" "${THIRD_PARTY_DIR}/meshoptimizer/src/meshoptimizer.h")
add_library(meshoptimizer STATIC ${MESHOPTIMIZER_SRC})
target_include_directories(meshoptimizer PUBLIC "${THIRD_PARTY_DIR}/meshoptimizer/src" "${THIRD_PARTY_DIR}/meshoptimizer")
set_property(TARGET meshoptimizer PROPERTY FOLDER "3rd/meshoptimizer")

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
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_vulkan.h"
        "${IMGUI_DIR}/imgui/backends/imgui_impl_vulkan.cpp"
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
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Scripting.cmake")
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
file(GLOB_RECURSE PROJECT_ABSOLUTE_HEADERS CONFIGURE_DEPENDS "${MAIN_SOURCE_DIR}/*.hpp" "${MAIN_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_ABSOLUTE_SOURCES CONFIGURE_DEPENDS "${MAIN_SOURCE_DIR}/*.cpp" "${MAIN_SOURCE_DIR}/*.c" "${MAIN_SOURCE_DIR}/*.cxx")

if(NOT USE_OPENGL)
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]gl[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]gl[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendGl\\.cpp$")
endif()
if(NOT USE_VULKAN)
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]vk[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]vk[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendVk\\.cpp$")
endif()
if(NOT USE_DX12)
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]dx12[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]dx12[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendDx12\\.cpp$")
endif()
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]metal[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]metal[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendMetal\\.mm$")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]platform[/\\\\]quest[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]platform[/\\\\]quest[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "androidAssetFileSystem\\.cpp$")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "androidAssetFileSystem\\.h$")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "androidAssetFileSystem\\.h$")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "oxrInput\\.cpp$")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "oxrInput\\.h$")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "oxrInput\\.h$")

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

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/VectorGraphics.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Coroutines.cmake")

add_executable(${APP_NAME} ${EXECUTABLE_FLAG}
    ${PROJECT_ABSOLUTE_HEADERS}
    ${PROJECT_ABSOLUTE_SOURCES}
)

# --- Generate Reflection Headers ---
add_custom_command(
    TARGET ${APP_NAME} PRE_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../utils/build/IkigaiHeaderTool.py" "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
    COMMENT "Running IkigaiHeaderTool to generate reflection headers..."
    VERBATIM
)

# Properties
set_target_properties(${APP_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(${APP_NAME} PRIVATE /bigobj /MP12 /Zm200)

# Definitions
target_compile_definitions(${APP_NAME} PRIVATE NOMINMAX)
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
target_compile_definitions(${APP_NAME} PRIVATE USE_CHEATS)
target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_VG_COOKER)
target_link_libraries(${APP_NAME} PRIVATE meshoptimizer)
if(USE_EDITOR)
    target_compile_definitions(${APP_NAME} PRIVATE USE_EDITOR)
endif()
if(USE_EDITOR AND USE_FILE_WATCHER)
    target_compile_definitions(${APP_NAME} PRIVATE USE_FILE_WATCHER)
endif()

# --- Includes and Linking ---

target_include_directories(${APP_NAME} PRIVATE
    ${MAIN_SOURCE_DIR}/engine
    ${THIRD_PARTY_DIR}/glm
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
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Upscalers.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Materials.cmake")
ikigai_attach_material_assets(${APP_NAME})

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
target_link_libraries(${APP_NAME} PRIVATE ikigai_coroutines)
if(IKIGAI_VECTOR_TESTS)
    add_executable(ikigai_vector_components_tests "${CMAKE_CURRENT_SOURCE_DIR}/../utils/tests/vector_components.cpp")
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

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Networking.cmake")
ikigai_configure_network(${APP_NAME})

# Shared application startup and optional local diagnostics.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Application.cmake")
ikigai_configure_application(${APP_NAME})
