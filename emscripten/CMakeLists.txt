cmake_minimum_required(VERSION 3.20)

set(APP_NAME "IkigaiEngine")
project(${APP_NAME} LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This CMake project must be configured with emcmake for the Emscripten toolchain")
endif()

# --- Configuration Options ---
option(USE_SDL      "Use SDL window backend" ON)
option(USE_OPENGL   "Use OpenGL rendering backend" ON)
option(USE_VULKAN   "Use Vulkan rendering backend" OFF)
option(USE_DX12     "Use DX12 rendering backend" OFF)
option(USE_METAL    "Use Metal rendering backend" OFF)
option(USE_MIMALLOC "Use mimalloc override" OFF)
option(USE_EDITOR "Build with in-engine editor UI" ON)
cmake_dependent_option(USE_FILE_WATCHER "Watch asset files for hot reload" OFF "USE_EDITOR" OFF)

set(USE_VULKAN OFF CACHE BOOL "Vulkan is not supported on Emscripten" FORCE)
set(USE_DX12 OFF CACHE BOOL "DX12 is not supported on Emscripten" FORCE)
set(USE_METAL OFF CACHE BOOL "Metal is not supported on Emscripten" FORCE)

if(NOT USE_OPENGL)
    message(FATAL_ERROR "Emscripten build requires USE_OPENGL=ON")
endif()

# --- Directory Setup ---
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rd")
set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(ENGINE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out" CACHE PATH "Directory for the built executable")
if(NOT IS_ABSOLUTE "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}")
    get_filename_component(ENGINE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_RUNTIME_OUTPUT_DIRECTORY}" ABSOLUTE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ENGINE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_EXECUTABLE_SUFFIX ".html")

# --- Dependencies ---

# 1. Assimp (trim unused importers to keep wasm smaller)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_PLY_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DRACO OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/assimp ./3rd/assimp/build-emscripten)

# 2. Freetype is provided by the Emscripten port (-sUSE_FREETYPE=1)

# 3. GLSLang
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/glslang ./3rd/glslang/build)

# 4. SPIRV-Cross
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
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

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}/imgui"
    "${IMGUI_DIR}/IconFont"
)

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

# 9. SDL3 is provided by the Emscripten port (-sUSE_SDL=3)

# 10. SoLoud (miniaudio, no SDL)
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
target_compile_definitions(soloud PRIVATE NOMINMAX)

# --- Main Application ---

file(GLOB_RECURSE PROJECT_ABSOLUTE_HEADERS "${MAIN_SOURCE_DIR}/*.hpp" "${MAIN_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_ABSOLUTE_SOURCES "${MAIN_SOURCE_DIR}/*.cpp" "${MAIN_SOURCE_DIR}/*.c" "${MAIN_SOURCE_DIR}/*.cxx")

if(NOT USE_OPENGL)
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]gl[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]gl[/\\\\]")
    list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendGl\\.cpp$")
endif()
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]vk[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]vk[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendVk\\.cpp$")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]dx12[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]dx12[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendDx12\\.cpp$")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]metal[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]metal[/\\\\]")
list(FILTER PROJECT_ABSOLUTE_SOURCES EXCLUDE REGEX "imguiBackendMetal\\.mm$")

add_executable(${APP_NAME}
    ${PROJECT_ABSOLUTE_HEADERS}
    ${PROJECT_ABSOLUTE_SOURCES}
)

add_custom_command(
    TARGET ${APP_NAME} PRE_BUILD
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/../utils/IkigaiHeaderTool.py" "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
    COMMENT "Running IkigaiHeaderTool to generate reflection headers..."
)

set_target_properties(${APP_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

target_compile_definitions(${APP_NAME} PRIVATE NOMINMAX)
target_compile_definitions(${APP_NAME} PRIVATE SOL_NO_EXCEPTIONS=1)
target_compile_definitions(${APP_NAME} PRIVATE $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>)
target_compile_definitions(${APP_NAME} PRIVATE EMSCRIPTEN __EMSCRIPTEN__ USING_GLES OPENGL_SIMPLE_RENDER)

if(USE_SDL)
    target_compile_definitions(${APP_NAME} PRIVATE USE_SDL)
endif()

if(USE_OPENGL)
    target_compile_definitions(${APP_NAME} PRIVATE IKIGAI_HAS_OPENGL OPENGL_BACKEND)
endif()

target_compile_definitions(${APP_NAME} PRIVATE USE_CHEATS)
if(USE_EDITOR)
    target_compile_definitions(${APP_NAME} PRIVATE USE_EDITOR)
endif()
if(USE_EDITOR AND USE_FILE_WATCHER)
    target_compile_definitions(${APP_NAME} PRIVATE USE_FILE_WATCHER)
endif()

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
    ${THIRD_PARTY_DIR}/stb
)

set(LIBS_TO_LINK
    imgui
    spine
    lua
    assimp
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

target_link_libraries(${APP_NAME} PRIVATE ${LIBS_TO_LINK})

# --- Emscripten compile / link ---
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
set(EMSCRIPTEN_PORT_OPTIONS
    "SHELL:-sUSE_SDL=3"
    "SHELL:-sUSE_FREETYPE=1"
)
set(EMSCRIPTEN_COMPILE_OPTIONS
    -fexceptions
    ${EMSCRIPTEN_PORT_OPTIONS}
)
set(EMSCRIPTEN_LINK_OPTIONS
    -fexceptions
    ${EMSCRIPTEN_PORT_OPTIONS}
    "SHELL:-sWASM=1"
    "SHELL:-sUSE_WEBGL2=1"
    "SHELL:-sMIN_WEBGL_VERSION=2"
    "SHELL:-sFULL_ES3=1"
    "SHELL:-sOFFSCREEN_FRAMEBUFFER=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sINITIAL_MEMORY=64MB"
    "SHELL:-sSTACK_SIZE=8MB"
    "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free"
    "SHELL:-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPF32,HEAPU8,wasmMemory"
    "SHELL:--preload-file ${ASSETS_DIR}@/assets"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/template.html"
    $<$<CONFIG:Debug>:SHELL:-sASSERTIONS=1>
    $<$<CONFIG:Release>:-O2>
)

target_compile_options(imgui PRIVATE ${EMSCRIPTEN_COMPILE_OPTIONS})
target_compile_options(${APP_NAME} PRIVATE ${EMSCRIPTEN_COMPILE_OPTIONS})
target_link_options(${APP_NAME} PRIVATE ${EMSCRIPTEN_LINK_OPTIONS})
