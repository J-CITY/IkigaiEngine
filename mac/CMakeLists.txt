cmake_minimum_required(VERSION 3.20)

set(APP_NAME "IkigaiEngine")
project(${APP_NAME} LANGUAGES C CXX)

# --- Configuration Options ---
option(USE_SDL2     "Use SDL2 window backend" ON)
option(USE_OPENGL   "Use OpenGL rendering backend" ON)
option(USE_VULKAN   "Use Vulkan rendering backend" OFF)
option(USE_METAL    "Use Metal rendering backend" OFF)
option(USE_MIMALLOC "Use mimalloc override" OFF)

if(NOT USE_OPENGL AND NOT USE_VULKAN AND NOT USE_METAL)
    message(FATAL_ERROR "At least one render backend must be enabled (USE_OPENGL, USE_VULKAN, USE_METAL)")
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")

# --- Directory Setup ---
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd")
set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out")

# Add Homebrew paths to CMake prefix paths for searching
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")

# --- Dependencies ---

# 1. Assimp
set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

# Fix for new Apple Clang treating nontrivial-memaccess as an error
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nontrivial-memaccess -Wno-error=nontrivial-memaccess")

add_subdirectory(${THIRD_PARTY_DIR}/assimp ./3rd/assimp/build)

# 2. Freetype
add_subdirectory(${THIRD_PARTY_DIR}/freetype ./3rd/freetype/build)

# 3. GLSLang
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
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

if(USE_METAL)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_metal.h"
        "${IMGUI_DIR}/imgui/backends/imgui_impl_metal.mm"
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

# 10. SDL2
if(USE_SDL2)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY_DIR}/SDL2 ./3rd/SDL2/build)
endif()

# 10. SoLoud
# On Windows there is a precompiled soloud_static_x64_d.lib, for macOS we build from source.
file(GLOB_RECURSE SOLOUD_SOURCES 
    "${THIRD_PARTY_DIR}/soloud/src/audiosource/*.cpp"
    "${THIRD_PARTY_DIR}/soloud/src/audiosource/*.c"
    "${THIRD_PARTY_DIR}/soloud/src/core/*.cpp"
    "${THIRD_PARTY_DIR}/soloud/src/filter/*.cpp"
)
if(USE_SDL2)
    list(APPEND SOLOUD_SOURCES "${THIRD_PARTY_DIR}/soloud/src/backend/sdl/soloud_sdl2.cpp" "${THIRD_PARTY_DIR}/soloud/src/backend/sdl/soloud_sdl2_dll.c")
else()
    list(APPEND SOLOUD_SOURCES "${THIRD_PARTY_DIR}/soloud/src/backend/coreaudio/soloud_coreaudio.cpp")
endif()

add_library(soloud STATIC ${SOLOUD_SOURCES})
target_include_directories(soloud PUBLIC "${THIRD_PARTY_DIR}/soloud/include")
if(USE_SDL2)
    target_compile_definitions(soloud PUBLIC WITH_SDL2)
else()
    target_compile_definitions(soloud PUBLIC WITH_COREAUDIO)
endif()

# --- Main Application ---

# Sources
file(GLOB_RECURSE PROJECT_ABSOLUTE_HEADERS "${MAIN_SOURCE_DIR}/*.hpp" "${MAIN_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_ABSOLUTE_SOURCES "${MAIN_SOURCE_DIR}/*.cpp" "${MAIN_SOURCE_DIR}/*.c" "${MAIN_SOURCE_DIR}/*.cxx" "${MAIN_SOURCE_DIR}/*.mm" "${MAIN_SOURCE_DIR}/*.m")

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
if(NOT USE_METAL)
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]metal[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]metal[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendMetal\\.mm$")
endif()
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]dx12[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]dx12[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendDx12\\.cpp$")

# Source Groups for IDE
source_group(TREE ${MAIN_SOURCE_DIR} FILES ${PROJECT_ABSOLUTE_HEADERS} ${PROJECT_ABSOLUTE_SOURCES})

add_executable(${APP_NAME} MACOSX_BUNDLE
    ${PROJECT_ABSOLUTE_HEADERS}
    ${PROJECT_ABSOLUTE_SOURCES}
)

# --- Generate Reflection Headers ---
add_custom_command(
    TARGET ${APP_NAME} PRE_BUILD
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../utils/IkigaiHeaderTool.py" "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
    COMMENT "Running IkigaiHeaderTool to generate reflection headers..."
)

# Properties
set_target_properties(${APP_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra)

# Definitions
target_compile_definitions(${APP_NAME} PRIVATE SOL_EXCEPTIONS_SAFE_PROPAGATION=1)

if(USE_SDL2)
    target_compile_definitions(${APP_NAME} PRIVATE USE_SDL)
endif()

if(USE_OPENGL)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_OPENGL OPENGL_BACKEND)
endif()

if(USE_VULKAN)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_VULKAN VULKAN_BACKEND)
endif()

if(USE_METAL)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_METAL METAL_BACKEND)
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
    soloud
)

if(USE_SDL2)
    target_link_libraries(${APP_NAME} PRIVATE SDL2::SDL2main SDL2::SDL2-static)
    target_include_directories(imgui PRIVATE "${THIRD_PARTY_DIR}/SDL2/include")
    target_include_directories(soloud PRIVATE "${THIRD_PARTY_DIR}/SDL2/include")
    target_include_directories(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/SDL2/include")
endif()

if(USE_OPENGL)
    find_package(OpenGL REQUIRED)
    target_link_libraries(${APP_NAME} PRIVATE OpenGL::GL)
    
    find_package(GLEW REQUIRED)
    target_include_directories(${APP_NAME} PRIVATE ${GLEW_INCLUDE_DIRS})
    target_link_libraries(${APP_NAME} PRIVATE GLEW::GLEW)
endif()

if(USE_VULKAN)
    target_link_libraries(${APP_NAME} PRIVATE volk)
endif()

# System frameworks
find_library(COCOA_FRAMEWORK Cocoa)
find_library(IOKIT_FRAMEWORK IOKit)
find_library(COREVIDEO_FRAMEWORK CoreVideo)
find_library(COREAUDIO_FRAMEWORK CoreAudio)
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
find_library(METAL_FRAMEWORK Metal)
find_library(METALKIT_FRAMEWORK MetalKit)

target_link_libraries(${APP_NAME} PRIVATE 
    ${LIBS_TO_LINK}
    ${COCOA_FRAMEWORK} 
    ${IOKIT_FRAMEWORK} 
    ${COREVIDEO_FRAMEWORK} 
    ${COREAUDIO_FRAMEWORK}
    ${AUDIOTOOLBOX_FRAMEWORK}
)

if(USE_METAL)
    target_link_libraries(${APP_NAME} PRIVATE ${METAL_FRAMEWORK} ${METALKIT_FRAMEWORK})
    # Tell CMake to compile core.cpp as Objective-C++ since it includes Metal headers
    set_source_files_properties(${MAIN_SOURCE_DIR}/engine/coreModule/core/core.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()
