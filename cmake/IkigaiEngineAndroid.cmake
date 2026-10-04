# Android shared library for IkigaiEngine.
# Included from cmake/android/CMakeLists.txt. Gradle passes -DIKIGAI_GLES_VERSION=300|310|320.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(THIRD_PARTY_DIR "${REPO_ROOT}/3rd")
set(MAIN_SOURCE_DIR "${REPO_ROOT}/src")

option(USE_EDITOR "Build with in-engine editor UI" ON)
cmake_dependent_option(USE_FILE_WATCHER "Watch asset files for hot reload" OFF "USE_EDITOR" OFF)

set(IKIGAI_GLES_VERSION "320" CACHE STRING "OpenGL ES version: 300, 310, or 320")
set_property(CACHE IKIGAI_GLES_VERSION PROPERTY STRINGS 300 310 320)
if(NOT IKIGAI_GLES_VERSION MATCHES "^(300|310|320)$")
    message(FATAL_ERROR "IKIGAI_GLES_VERSION must be 300, 310, or 320 (got '${IKIGAI_GLES_VERSION}')")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Debug builds must still produce libmain.so / libSDL3.so (SDLActivity loads "main", "SDL3").
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)

add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
)

# --- Third-party (static, except SDL3) ---

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
add_subdirectory("${THIRD_PARTY_DIR}/assimp" "${CMAKE_BINARY_DIR}/3rd/assimp")

set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY_DIR}/freetype" "${CMAKE_BINARY_DIR}/3rd/freetype")

set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY_DIR}/glslang" "${CMAKE_BINARY_DIR}/3rd/glslang")

set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY_DIR}/SPIRV-Cross" "${CMAKE_BINARY_DIR}/3rd/SPIRV-Cross")

file(GLOB SPIRV_REFLECT_SRC
    "${THIRD_PARTY_DIR}/SPIRV-Reflect/spirv_reflect.cpp"
    "${THIRD_PARTY_DIR}/SPIRV-Reflect/spirv_reflect.h"
)
add_library(spirv-reflect STATIC ${SPIRV_REFLECT_SRC})
target_include_directories(spirv-reflect PUBLIC "${THIRD_PARTY_DIR}/SPIRV-Reflect")

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
    "${IMGUI_DIR}/imgui/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIR}/imgui/backends/imgui_impl_sdl3.h"
    "${IMGUI_DIR}/imgui/backends/imgui_impl_opengl3.cpp"
    "${IMGUI_DIR}/imgui/backends/imgui_impl_opengl3.h"
    "${IMGUI_DIR}/imgui/backends/imgui_impl_opengl3_loader.h"
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}/imgui"
    "${IMGUI_DIR}/IconFont"
    "${THIRD_PARTY_DIR}/SDL3/include"
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3 IKIGAI_GLES_VERSION=${IKIGAI_GLES_VERSION})

file(GLOB_RECURSE SPINE_FILES "${THIRD_PARTY_DIR}/spine/spine/*.cpp" "${THIRD_PARTY_DIR}/spine/spine/*.h")
add_library(spine STATIC ${SPINE_FILES})
target_include_directories(spine PUBLIC "${THIRD_PARTY_DIR}/spine")
target_compile_definitions(spine PUBLIC SPINE_USE_STD_FUNCTION)

file(GLOB_RECURSE LUA_FILES "${THIRD_PARTY_DIR}/lua/*.c")
list(FILTER LUA_FILES EXCLUDE REGEX "lua\\.c$")
list(FILTER LUA_FILES EXCLUDE REGEX "luac\\.c$")
add_library(lua STATIC ${LUA_FILES})
target_include_directories(lua PUBLIC "${THIRD_PARTY_DIR}/lua")

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

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY_DIR}/SDL3" "${CMAKE_BINARY_DIR}/3rd/SDL3")
if(TARGET SDL3-shared)
    set_target_properties(SDL3-shared PROPERTIES DEBUG_POSTFIX "")
endif()

# --- Engine shared library (SDL loads it as "main") ---

file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS "${MAIN_SOURCE_DIR}/*.hpp" "${MAIN_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS "${MAIN_SOURCE_DIR}/*.cpp" "${MAIN_SOURCE_DIR}/*.c" "${MAIN_SOURCE_DIR}/*.cxx")

foreach(backend vk dx12 metal)
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]${backend}[/\\\\]")
    list(FILTER PROJECT_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]${backend}[/\\\\]")
endforeach()
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "imguiBackendVk\\.cpp$")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "imguiBackendDx12\\.cpp$")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "imguiBackendMetal\\.mm$")

add_library(main SHARED ${PROJECT_HEADERS} ${PROJECT_SOURCES})
set_target_properties(main PROPERTIES DEBUG_POSTFIX "")
# NDK enables -Werror=format-security. Editor/ImGui helpers pass non-literal format strings.
target_compile_options(main PRIVATE -Wno-error=format-security)

add_custom_command(
    TARGET main PRE_BUILD
    COMMAND python "${REPO_ROOT}/utils/IkigaiHeaderTool.py" "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
    COMMENT "Running IkigaiHeaderTool to generate reflection headers..."
)

target_compile_definitions(main PRIVATE
    NOMINMAX
    USE_SDL
    IKIGAI_HAS_OPENGL
    OPENGL_BACKEND
    USING_GLES
    IMGUI_IMPL_OPENGL_ES3
    IKIGAI_GLES_VERSION=${IKIGAI_GLES_VERSION}
    USE_CHEATS
    SOL_EXCEPTIONS_SAFE_PROPAGATION=1
    $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>
)
if(USE_EDITOR)
    target_compile_definitions(main PRIVATE USE_EDITOR)
endif()
if(USE_EDITOR AND USE_FILE_WATCHER)
    target_compile_definitions(main PRIVATE USE_FILE_WATCHER)
endif()

target_include_directories(main PRIVATE
    "${MAIN_SOURCE_DIR}/engine"
    "${THIRD_PARTY_DIR}/glm"
    "${THIRD_PARTY_DIR}/serdepp/include"
    "${THIRD_PARTY_DIR}/magic_enum/include"
    "${THIRD_PARTY_DIR}/magic_enum/include/magic_enum"
    "${THIRD_PARTY_DIR}/nameof/include"
    "${THIRD_PARTY_DIR}/json/single_include"
    "${THIRD_PARTY_DIR}/soloud/include"
    "${THIRD_PARTY_DIR}/fmt/include"
    "${THIRD_PARTY_DIR}/vfspp/include"
    "${THIRD_PARTY_DIR}/vfspp/vendor/miniz-cpp"
    "${THIRD_PARTY_DIR}/sol/include"
    "${THIRD_PARTY_DIR}/stb"
    "${THIRD_PARTY_DIR}/SDL3/include"
)

find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_LIB android)

target_link_libraries(main PRIVATE
    SDL3::SDL3
    GLESv3
    EGL
    ${ANDROID_LOG_LIB}
    ${ANDROID_LIB}
    imgui
    spine
    lua
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

message(STATUS "IkigaiEngine Android OpenGL ES IKIGAI_GLES_VERSION=${IKIGAI_GLES_VERSION}")
