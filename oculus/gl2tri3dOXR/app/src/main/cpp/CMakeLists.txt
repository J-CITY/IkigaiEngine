cmake_minimum_required(VERSION 3.4.1)

set(PROJTOP ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../)

# Location of our third party libraries.
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd")

# Location of our own shared C++ code base.
set(MAIN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../src")

set(OXR_KHRONOS_DIR ${THIRD_PARTY_DIR}/OpenXR-SDK)
set(OXR_LOADER_DIR  ${THIRD_PARTY_DIR}/Oculus_OpenXR_Mobile_SDK)

# ------------------------------------------------------------------------
#    native_app_glue
# ------------------------------------------------------------------------
set(${CMAKE_C_FLAGS}, "${CMAKE_C_FLAGS}")
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

# now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -Wimplicit-function-declaration -fexceptions -D WITH_MINIAUDIO") # -Wall -Werror

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")


# ------------------------------------------------------------------------
#    openxr_loader
# ------------------------------------------------------------------------
add_library(openxr_loader SHARED IMPORTED)
set_property(TARGET openxr_loader
     PROPERTY IMPORTED_LOCATION
     ${OXR_LOADER_DIR}/OpenXR/Libs/Android/${ANDROID_ABI}/${CMAKE_BUILD_TYPE}/libopenxr_loader.so)

add_library(
        assimp
        SHARED
        IMPORTED
)

set_target_properties(
        assimp
        PROPERTIES
        IMPORTED_LOCATION
        ${PROJTOP}/oculus/lib/libassimp.so
)

# We will tell our build system where to look for third party header files.
include_directories(${THIRD_PARTY_DIR}/glm)
include_directories(${THIRD_PARTY_DIR}/tiny-obj-loader)
include_directories(${THIRD_PARTY_DIR}/serdepp/include)
include_directories(${THIRD_PARTY_DIR}/magic_enum/include)
include_directories(${THIRD_PARTY_DIR}/magic_enum/include/magic_enum)
include_directories(${THIRD_PARTY_DIR}/nameof/include)
include_directories(${THIRD_PARTY_DIR}/json/single_include)
include_directories(${THIRD_PARTY_DIR}/assimp/include)

include_directories(${THIRD_PARTY_DIR}/soloud/include)
include_directories(${THIRD_PARTY_DIR}/sol/single/include)
include_directories(${THIRD_PARTY_DIR}/lua)
include_directories(${THIRD_PARTY_DIR}/freetype/include)
include_directories(${THIRD_PARTY_DIR}/spine)
include_directories(${THIRD_PARTY_DIR}/stb)
# global engine include
include_directories(${MAIN_SOURCE_DIR}/engine)

add_subdirectory(${THIRD_PARTY_DIR}/freetype ${THIRD_PARTY_DIR}/freetype/oculusBuild)


file(GLOB_RECURSE SOLOUD_FILES
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_audiosource.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_bus.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_3d.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_basicops.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_faderops.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_filterops.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_getters.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_setters.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_voicegroup.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_core_voiceops.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_fader.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_fft_lut.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_fft.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_file.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_filter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_misc.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_queue.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud_thread.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/core/soloud.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/audiosource/wav/dr_impl.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/audiosource/wav/soloud_wav.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/audiosource/wav/soloud_wavstream.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/audiosource/wav/stb_vorbis.c"
        #"../../3rd/soloud/src/backend/opensles/soloud_opensles.cpp"
        #"../../3rd/soloud/src/backend/sdl_static/soloud_sdl_static.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/backend/miniaudio/soloud_miniaudio.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_bassboostfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_biquadresonantfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_dcremovalfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_duckfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_echofilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_eqfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_fftfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_flangerfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_freeverbfilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_lofifilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_robotizefilter.cpp"
        "${THIRD_PARTY_DIR}/soloud/src/filter/soloud_waveshaperfilter.cpp"
        )
file(GLOB_RECURSE SPINE_FILES
        "${THIRD_PARTY_DIR}/spine/spine/*.h"
        "${THIRD_PARTY_DIR}/spine/spine/*.cpp"
        )

file(GLOB_RECURSE LUA_FILES
        "${THIRD_PARTY_DIR}/lua/lapi.c"
        "${THIRD_PARTY_DIR}/lua/lcode.c"
        "${THIRD_PARTY_DIR}/lua/lctype.c"
        "${THIRD_PARTY_DIR}/lua/ldebug.c"
        "${THIRD_PARTY_DIR}/lua/ldo.c"
        "${THIRD_PARTY_DIR}/lua/ldump.c"
        "${THIRD_PARTY_DIR}/lua/lfunc.c"
        "${THIRD_PARTY_DIR}/lua/lgc.c"
        "${THIRD_PARTY_DIR}/lua/llex.c"
        "${THIRD_PARTY_DIR}/lua/lmem.c"
        "${THIRD_PARTY_DIR}/lua/lobject.c"
        "${THIRD_PARTY_DIR}/lua/lopcodes.c"
        "${THIRD_PARTY_DIR}/lua/lparser.c"
        "${THIRD_PARTY_DIR}/lua/lstate.c"
        "${THIRD_PARTY_DIR}/lua/lstring.c"
        "${THIRD_PARTY_DIR}/lua/ltable.c"
        "${THIRD_PARTY_DIR}/lua/ltm.c"
        "${THIRD_PARTY_DIR}/lua/lundump.c"
        "${THIRD_PARTY_DIR}/lua/lvm.c"
        "${THIRD_PARTY_DIR}/lua/lzio.c"
        "${THIRD_PARTY_DIR}/lua/lauxlib.c"
        "${THIRD_PARTY_DIR}/lua/lbaselib.c"
        "${THIRD_PARTY_DIR}/lua/lcorolib.c"
        "${THIRD_PARTY_DIR}/lua/ldblib.c"
        "${THIRD_PARTY_DIR}/lua/liolib.c"
        "${THIRD_PARTY_DIR}/lua/lmathlib.c"
        "${THIRD_PARTY_DIR}/lua/loadlib.c"
        "${THIRD_PARTY_DIR}/lua/loslib.c"
        "${THIRD_PARTY_DIR}/lua/lstrlib.c"
        "${THIRD_PARTY_DIR}/lua/ltablib.c"
        "${THIRD_PARTY_DIR}/lua/lutf8lib.c"
        "${THIRD_PARTY_DIR}/lua/linit.c"
        )


add_library(soloud STATIC ${SOLOUD_FILES})
add_library(spine STATIC ${SPINE_FILES})
target_compile_definitions(spine PUBLIC SPINE_USE_STD_FUNCTION)
add_library(lua STATIC ${LUA_FILES})
# ------------------------------------------------------------------------
#    openxr_app
# ------------------------------------------------------------------------

file(GLOB LOCAL_HEADERS "*.h")
file(GLOB LOCAL_SOURCE  "*.cpp")

file(GLOB_RECURSE CPP_HEADERS ${MAIN_SOURCE_DIR}/*.hpp)
file(GLOB_RECURSE CPP_SOURCES ${MAIN_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE CPP_HEADERS2 ${MAIN_SOURCE_DIR}/*.h)

add_library(openxr_app SHARED
    ${CPP_HEADERS}
    ${CPP_SOURCES}
    ${CPP_HEADERS2}
    ${LOCAL_SOURCE}
    ${LOCAL_HEADERS}
     ${PROJTOP}/oculus/common/util_egl.c
     ${PROJTOP}/oculus/common/util_oxr.cpp
     ${PROJTOP}/oculus/common/util_shader.c
     ${PROJTOP}/oculus/common/util_matrix.c
     ${PROJTOP}/oculus/common/util_debugstr.c
     ${PROJTOP}/oculus/common/assertegl.c
     ${PROJTOP}/oculus/common/assertgl.c
     ${PROJTOP}/oculus/common/winsys/winsys_null.c
    )

target_include_directories(openxr_app PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
    ${OXR_KHRONOS_DIR}/include/
    ${OXR_KHRONOS_DIR}/src/
    ${PROJTOP}/oculus/common/
    ${PROJTOP}/oculus/common/winsys/
    ${THIRD_PARTY_DIR}/Oculus_OpenXR_Mobile_SDK/OpenXR/Include/
    ${THIRD_PARTY_DIR}/OpenXR-SDK/include/
    )
# DEFINES
add_definitions(-DNOMINMAX)
add_definitions(-DOCULUS)
add_definitions(-DXR_OS_ANDROID)
add_definitions(-DXR_USE_PLATFORM_ANDROID)
add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)

# if (backend == opengl)
add_definitions(-DOPENGL_BACKEND)
add_definitions(-DUSING_GLES)


# add lib dependencies
target_link_libraries(openxr_app
    android
    native_app_glue
    openxr_loader
    EGL
    GLESv3
    assimp
    log
    freetype
    spine
    lua
    soloud)




