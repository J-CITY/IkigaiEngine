# Quest / OpenXR shared library for IkigaiEngine.
# Included from cmake/quest/CMakeLists.txt.

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(THIRD_PARTY_DIR "${REPO_ROOT}/3rd")
set(MAIN_SOURCE_DIR "${REPO_ROOT}/src")
set(QUEST_COMMON_DIR "${REPO_ROOT}/oculus/common")
set(OPENXR_SDK_DIR "${THIRD_PARTY_DIR}/OpenXR-SDK")
set(META_OPENXR_DIR "${THIRD_PARTY_DIR}/MetaOpenXR")

set(IKIGAI_GLES_VERSION "320" CACHE STRING "OpenGL ES version: 300, 310, or 320")
set_property(CACHE IKIGAI_GLES_VERSION PROPERTY STRINGS 300 310 320)
if(NOT IKIGAI_GLES_VERSION MATCHES "^(300|310|320)$")
    message(FATAL_ERROR "IKIGAI_GLES_VERSION must be 300, 310, or 320 (got '${IKIGAI_GLES_VERSION}')")
endif()

if(NOT EXISTS "${OPENXR_SDK_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR "OpenXR-SDK headers not found at ${OPENXR_SDK_DIR}. Run: git submodule update --init 3rd/OpenXR-SDK")
endif()

set(_OX_LOADER "${META_OPENXR_DIR}/OpenXR/Libs/Android/${ANDROID_ABI}/${CMAKE_BUILD_TYPE}/libopenxr_loader.so")
if(NOT EXISTS "${_OX_LOADER}")
    set(_OX_LOADER "${META_OPENXR_DIR}/OpenXR/Libs/Android/${ANDROID_ABI}/libopenxr_loader.so")
endif()
if(NOT EXISTS "${_OX_LOADER}")
    message(FATAL_ERROR "libopenxr_loader.so not found for ${ANDROID_ABI}. Run: python utils/fetch_meta_openxr.py")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "" FORCE)

add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

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

add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION "${_OX_LOADER}")

file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS "${MAIN_SOURCE_DIR}/*.hpp" "${MAIN_SOURCE_DIR}/*.h")
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS "${MAIN_SOURCE_DIR}/*.cpp" "${MAIN_SOURCE_DIR}/*.c" "${MAIN_SOURCE_DIR}/*.cxx")

foreach(backend vk dx12 metal)
    list(FILTER PROJECT_SOURCES EXCLUDE REGEX "[/\\\\]backends[/\\\\]${backend}[/\\\\]")
    list(FILTER PROJECT_HEADERS EXCLUDE REGEX "[/\\\\]backends[/\\\\]${backend}[/\\\\]")
endforeach()
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "imguiBackend")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "imguiBackendMetal\\.mm$")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "[/\\\\]editorModule[/\\\\]")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "[/\\\\]debugModule[/\\\\]")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "[/\\\\]imguiHelper[/\\\\]")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "sdlFileSystem\\.cpp$")
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "[/\\\\]main\\.cpp$")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX "[/\\\\]editorModule[/\\\\]")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX "[/\\\\]debugModule[/\\\\]")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX "[/\\\\]imguiHelper[/\\\\]")

set(QUEST_SOURCES
    "${QUEST_COMMON_DIR}/util_egl.c"
    "${QUEST_COMMON_DIR}/util_oxr.cpp"
    "${QUEST_COMMON_DIR}/util_shader.c"
    "${QUEST_COMMON_DIR}/util_matrix.c"
    "${QUEST_COMMON_DIR}/util_debugstr.c"
    "${QUEST_COMMON_DIR}/assertegl.c"
    "${QUEST_COMMON_DIR}/assertgl.c"
    "${QUEST_COMMON_DIR}/winsys/winsys_null.c"
)

add_library(main SHARED ${PROJECT_HEADERS} ${PROJECT_SOURCES} ${QUEST_SOURCES})
set_target_properties(main PROPERTIES DEBUG_POSTFIX "")
target_compile_options(main PRIVATE -Wno-error=format-security)

add_custom_command(
    TARGET main PRE_BUILD
    COMMAND python "${REPO_ROOT}/utils/IkigaiHeaderTool.py" "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
    COMMENT "Running IkigaiHeaderTool to generate reflection headers..."
)

target_compile_definitions(main PRIVATE
    NOMINMAX
    OCULUS
    XR_OS_ANDROID
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
    IKIGAI_HAS_OPENGL
    OPENGL_BACKEND
    USING_GLES
    IKIGAI_GLES_VERSION=${IKIGAI_GLES_VERSION}
    SOL_EXCEPTIONS_SAFE_PROPAGATION=1
    $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>
)

target_include_directories(main PRIVATE
    "${MAIN_SOURCE_DIR}/engine"
    "${MAIN_SOURCE_DIR}"
    "${QUEST_COMMON_DIR}"
    "${QUEST_COMMON_DIR}/winsys"
    "${OPENXR_SDK_DIR}/include"
    "${OPENXR_SDK_DIR}/src"
    "${OPENXR_SDK_DIR}/src/common"
    "${META_OPENXR_DIR}/OpenXR/Include"
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
    ${ANDROID_NDK}/sources/android/native_app_glue
)

find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_LIB android)

target_link_libraries(main PRIVATE
    native_app_glue
    openxr_loader
    GLESv3
    EGL
    android
    ${ANDROID_LOG_LIB}
    ${ANDROID_LIB}
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

message(STATUS "IkigaiEngine Quest OpenXR GLES IKIGAI_GLES_VERSION=${IKIGAI_GLES_VERSION}")
message(STATUS "OpenXR loader: ${_OX_LOADER}")
