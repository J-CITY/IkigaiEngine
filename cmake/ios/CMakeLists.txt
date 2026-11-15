# Builds the ios application and its platform dependencies.
cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../AppleToolchain.cmake")
ikigai_configure_apple_toolchain(ios)

include(CMakeDependentOption)

set(APP_NAME "IkigaiEngine")
set(IOS_BUNDLE_IDENTIFIER "com.yourcompany.IkigaiEngine" CACHE STRING "iOS application bundle identifier")
set(IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team for device signing")
project(${APP_NAME} LANGUAGES C CXX)

# Disable code signing by default for all 3rd party targets (static libs)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")

# --- Configuration Options ---
include("${CMAKE_CURRENT_LIST_DIR}/../EngineOptions.cmake")
ikigai_configure_engine_options(ios)


if(NOT USE_SDL)
    message(FATAL_ERROR "The iOS target requires the SDL window backend")
endif()

if(USE_METAL)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# Silence availability errors for std::to_chars (used by fmt/spdlog via json/format)
add_compile_definitions(_LIBCPP_DISABLE_AVAILABILITY)


# --- Directory Setup ---
get_filename_component(IKIGAI_PLATFORM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(THIRD_PARTY_DIR "${IKIGAI_PLATFORM_ROOT}/3rd")
set(MAIN_SOURCE_DIR "${IKIGAI_PLATFORM_ROOT}/src")
include("${CMAKE_CURRENT_LIST_DIR}/../OutputDirectories.cmake")
ikigai_configure_output_directories(ios)

# --- Dependencies ---

# 1. Assimp
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
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

if(USE_METAL)
    list(APPEND IMGUI_SOURCES 
        "${IMGUI_DIR}/imgui/backends/imgui_impl_metal.h"
        "${IMGUI_DIR}/imgui/backends/imgui_impl_metal.mm"
    )
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_compile_options(imgui PRIVATE -Wno-deprecated-declarations -Wno-error=deprecated-declarations -Wno-error=format -Wno-error=format-security -Wno-format-security -w)
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
target_compile_definitions(lua PRIVATE LUA_USE_IOS)
endif()

# 9. Volk
add_library(volk STATIC "${THIRD_PARTY_DIR}/volk/volk.c" "${THIRD_PARTY_DIR}/volk/volk.h")
target_include_directories(volk PUBLIC "${THIRD_PARTY_DIR}/Vulkan-Headers/include")
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

# 10. SDL3
if(USE_SDL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
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
set_source_files_properties("${THIRD_PARTY_DIR}/soloud/src/backend/miniaudio/soloud_miniaudio.cpp" PROPERTIES COMPILE_FLAGS "-x objective-c++")


# --- Main Application ---
include("${CMAKE_CURRENT_LIST_DIR}/../EngineSources.cmake")
ikigai_collect_engine_sources("${MAIN_SOURCE_DIR}" PROJECT_ABSOLUTE_HEADERS PROJECT_ABSOLUTE_SOURCES OBJC)

include("${CMAKE_CURRENT_LIST_DIR}/../PlatformSources.cmake")
ikigai_filter_platform_sources(PROJECT_ABSOLUTE_HEADERS PROJECT_ABSOLUTE_SOURCES ios)

source_group(TREE ${MAIN_SOURCE_DIR} FILES ${PROJECT_ABSOLUTE_HEADERS} ${PROJECT_ABSOLUTE_SOURCES})

include("${CMAKE_CURRENT_LIST_DIR}/../VectorGraphics.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../EngineCore.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../EngineFeatures.cmake")

add_executable(${APP_NAME} MACOSX_BUNDLE
    ${PROJECT_ABSOLUTE_HEADERS}
    ${PROJECT_ABSOLUTE_SOURCES}
)

set_target_properties(${APP_NAME} PROPERTIES 
    MACOSX_BUNDLE_INFO_PLIST "${IKIGAI_PLATFORM_ROOT}/platform/ios/Info.plist.in"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" # iPhone and iPad
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${IOS_BUNDLE_IDENTIFIER}"
    "XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED[sdk=iphoneos*]" "YES"
    "XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED[sdk=iphoneos*]" "YES"
    "XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY[sdk=iphoneos*]" "Apple Development"
)
if(IOS_DEVELOPMENT_TEAM)
    set_property(TARGET ${APP_NAME} PROPERTY "XCODE_ATTRIBUTE_DEVELOPMENT_TEAM[sdk=iphoneos*]" "${IOS_DEVELOPMENT_TEAM}")
endif()

# --- Generate Reflection Headers ---
include("${CMAKE_CURRENT_LIST_DIR}/../Codegen.cmake")
ikigai_configure_reflection(${APP_NAME})
# iOS bundles are flat. Copy both content trees before Xcode signs the bundle.
set(ios_content_dir "${CMAKE_CURRENT_BINARY_DIR}/content/$<CONFIG>")
set(ios_bundle_dir "$<TARGET_BUNDLE_CONTENT_DIR:${APP_NAME}>")
add_custom_command(
    TARGET ${APP_NAME} PRE_LINK
    COMMAND "${Python3_EXECUTABLE}" "${IKIGAI_PLATFORM_ROOT}/utils/assets/content_packs.py"
        stage --platform ios --layout repo --dest "${ios_content_dir}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ios_bundle_dir}/assets" "${ios_bundle_dir}/assets_packs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ios_content_dir}/assets" "${ios_bundle_dir}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ios_content_dir}/assets_packs" "${ios_bundle_dir}/assets_packs"
    COMMENT "Staging iOS content packs..."
    VERBATIM
)

# Properties
set_target_properties(${APP_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra -Wno-deprecated-declarations -Wno-error=deprecated-declarations -Wno-error=format -Wno-error=format-security -Wno-format-security)

# Definitions
target_compile_definitions(${APP_NAME} PRIVATE SOL_EXCEPTIONS_SAFE_PROPAGATION=1 SOL_NO_LUA_HPP=1)

if(USE_SDL)
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
    foreach(src ${PROJECT_ABSOLUTE_SOURCES})
        if(src MATCHES "\\.(mm|m)$")
            set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        endif()
    endforeach()
endif()

# Additional definitions
target_compile_definitions(${APP_NAME} PRIVATE JSON_HAS_STD_FORMAT=0)
ikigai_apply_feature_definitions(${APP_NAME})
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

if(USE_SDL)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(${APP_NAME} PRIVATE SDL3::SDL3main)
    endif()
    target_link_libraries(${APP_NAME} PRIVATE SDL3::SDL3-static)
    target_include_directories(imgui PRIVATE "${THIRD_PARTY_DIR}/SDL3/include")
    target_include_directories(${APP_NAME} PRIVATE "${THIRD_PARTY_DIR}/SDL3/include")
endif()

if(USE_OPENGL)
    target_link_libraries(${APP_NAME} PRIVATE "-framework OpenGLES")
endif()

if(USE_VULKAN)
    target_compile_definitions(${APP_NAME} PRIVATE VK_NO_PROTOTYPES)
    target_link_libraries(${APP_NAME} PRIVATE volk)
endif()

# iOS System frameworks
target_link_libraries(${APP_NAME} PRIVATE 
    ${LIBS_TO_LINK}
    "-framework UIKit"
    "-framework QuartzCore"
    "-framework Foundation"
    "-framework CoreVideo"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AVFoundation"
)

if(USE_METAL)
    target_link_libraries(${APP_NAME} PRIVATE "-framework Metal" "-framework MetalKit")
    # Tell CMake to compile core.cpp as Objective-C++ since it includes Metal headers
    set_source_files_properties(${MAIN_SOURCE_DIR}/engine/coreModule/core/core.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

# Independent vector geometry and SDF font module
target_link_libraries(${APP_NAME} PRIVATE ikigai_vector)

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
