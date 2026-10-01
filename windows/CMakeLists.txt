#TODO 
# soloud сборка


cmake_minimum_required(VERSION 3.20)

set(APP_NAME "IkigaiEngine")
project(${APP_NAME} LANGUAGES C CXX)

# --- Configuration Options ---
option(USE_SDL2     "Use SDL2 window backend" ON)
option(USE_OPENGL   "Use OpenGL rendering backend" ON)
option(USE_VULKAN   "Use Vulkan rendering backend" OFF)
option(USE_DX12     "Use DX12 rendering backend" OFF)
option(USE_METAL    "Use Metal rendering backend" OFF)
option(USE_MIMALLOC "Use mimalloc override" OFF)

if(NOT USE_OPENGL AND NOT USE_VULKAN AND NOT USE_DX12)
    message(FATAL_ERROR "At least one render backend must be enabled (USE_OPENGL, USE_VULKAN, USE_DX12)")
endif()

# --- Directory Setup ---
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd")
set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out")

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
option(SPIRV_CROSS_SKIP_INSTALL ON)
option(SPIRV_CROSS_CLI OFF)
option(SPIRV_CROSS_ENABLE_TESTS OFF)
option(SPIRV_CROSS_ENABLE_C_API OFF)
option(SPIRV_CROSS_ENABLE_CPP OFF)
option(SPIRV_CROSS_ENABLE_UTIL OFF)
add_subdirectory(${THIRD_PARTY_DIR}/SPIRV-Cross ./3rd/SPIRV-Cross/build)

# 5. SPIRV-Reflect
file(GLOB SPIRV_REFLECT_SRC 
    "${THIRD_PARTY_DIR}/SPIRV-Reflect/spirv_reflect.cpp" 
    "${THIRD_PARTY_DIR}/SPIRV-Reflect/spirv_reflect.h"
)
add_library(spirv-reflect STATIC ${SPIRV_REFLECT_SRC})
target_include_directories(spirv-reflect PUBLIC "${THIRD_PARTY_DIR}/SPIRV-Reflect")
set_property(TARGET spirv-reflect PROPERTY FOLDER "3rd/SPIRV-Reflect")

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

if(USE_SDL2)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_sdl2.cpp" 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_sdl2.h"
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

if(USE_SDL2)
    target_include_directories(imgui PRIVATE "${THIRD_PARTY_DIR}/SDL2/include")
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
file(GLOB_RECURSE LUA_FILES "${THIRD_PARTY_DIR}/lua/*.c")
list(FILTER LUA_FILES EXCLUDE REGEX "lua\\.c$")
list(FILTER LUA_FILES EXCLUDE REGEX "luac\\.c$")
add_library(lua STATIC ${LUA_FILES})
target_include_directories(lua PUBLIC "${THIRD_PARTY_DIR}/lua")

# 9. Volk
add_library(volk STATIC "${THIRD_PARTY_DIR}/volk/volk.c" "${THIRD_PARTY_DIR}/volk/volk.h")
target_include_directories(volk PUBLIC "${THIRD_PARTY_DIR}/Vulkan-Headers/include")
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

# 10. SDL2
if(USE_SDL2)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY_DIR}/SDL2 ./3rd/SDL2/build)
endif()

# --- Main Application ---

# Sources
file(GLOB_RECURSE PROJECT_ABSOLUTE_HEADERS "${MAIN_SOURCE_DIR}/*.hpp" "${MAIN_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_ABSOLUTE_SOURCES "${MAIN_SOURCE_DIR}/*.cpp" "${MAIN_SOURCE_DIR}/*.c" "${MAIN_SOURCE_DIR}/*.cxx")

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

# Source Groups for IDE
source_group(TREE ${MAIN_SOURCE_DIR} FILES ${PROJECT_ABSOLUTE_HEADERS} ${PROJECT_ABSOLUTE_SOURCES})

if(WIN32)
    set(EXECUTABLE_FLAG WIN32) 
endif()

add_executable(${APP_NAME} ${EXECUTABLE_FLAG}
    ${PROJECT_ABSOLUTE_HEADERS}
    ${PROJECT_ABSOLUTE_SOURCES}
)

# --- Generate Reflection Headers ---
add_custom_command(
    TARGET ${APP_NAME} PRE_BUILD
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/../utils/IkigaiHeaderTool.py" "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
    COMMENT "Running IkigaiHeaderTool to generate reflection headers..."
)

# Properties
set_target_properties(${APP_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(${APP_NAME} PRIVATE /bigobj /MP12 /Zm200)

# Definitions
target_compile_definitions(${APP_NAME} PRIVATE NOMINMAX)
target_compile_definitions(${APP_NAME} PRIVATE SOL_EXCEPTIONS_SAFE_PROPAGATION=1)
target_compile_definitions(${APP_NAME} PRIVATE $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>)

if(USE_SDL2)
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
target_compile_definitions(${APP_NAME} PRIVATE USE_EDITOR USE_CHEATS)

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
    ${THIRD_PARTY_DIR}/sol/include
    ${THIRD_PARTY_DIR}/volk
    ${THIRD_PARTY_DIR}/Vulkan-Headers/include
    ${THIRD_PARTY_DIR}/VMA/include
    ${THIRD_PARTY_DIR}/D3D12MA/src
)

set(LIBS_TO_LINK
    imgui
    spine
    lua
    assimp
    freetype
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
endif()

if(USE_SDL2)
    target_link_libraries(${APP_NAME} PRIVATE SDL2::SDL2main SDL2::SDL2-static)
    target_include_directories(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/SDL2/include")
endif()

# SoLoud
target_link_libraries(${APP_NAME} PRIVATE 
    "${THIRD_PARTY_DIR}/soloud/lib/soloud_static_x64_d.lib"
)

target_link_libraries(${APP_NAME} PRIVATE ${LIBS_TO_LINK})

set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT /NODEFAULTLIB:LIBCMTD /ignore:4099")
