cmake_minimum_required(VERSION 3.7)

project(cross_test)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -fexceptions -O2 -s EXPORTED_FUNCTIONS=\"['_malloc','_main']\" -s EXPORTED_RUNTIME_METHODS=\"['ccall', 'cwrap']\"  -s USE_SDL=2 -D WITH_MINIAUDIO -s FULL_ES3=1 -s USE_WEBGL2=1 -s WASM=1 -s MIN_WEBGL_VERSION=2 -s GL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=1 -s OFFSCREEN_FRAMEBUFFER=1 -s STACK_SIZE=8MB -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=64MB -s ASSERTIONS -s USE_FREETYPE=1")

set(THIRD_PARTY_DIR "../3rd")
set(MAIN_SOURCE_DIR "../src")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out)

include_directories(${THIRD_PARTY_DIR}/SDL2/include)
include_directories(${THIRD_PARTY_DIR}/glm)
include_directories(${THIRD_PARTY_DIR}/tiny-obj-loader)
include_directories(${THIRD_PARTY_DIR}/SDL2_image)
include_directories(${THIRD_PARTY_DIR}/serdepp/include)
include_directories(${THIRD_PARTY_DIR}/magic_enum/include)
include_directories(${THIRD_PARTY_DIR}/nameof/include)
include_directories(${THIRD_PARTY_DIR}/json/single_include)
include_directories(${THIRD_PARTY_DIR}/vfspp/include)
include_directories(${THIRD_PARTY_DIR}/vfspp/vendor/miniz-cpp)

include_directories(${THIRD_PARTY_DIR}/assimp/include)
include_directories(./include) #assimp config
include_directories(${THIRD_PARTY_DIR}/soloud/include)
include_directories(${THIRD_PARTY_DIR}/sol/include)
include_directories(${THIRD_PARTY_DIR}/lua)
include_directories(${THIRD_PARTY_DIR}/freetype/include)
include_directories(${THIRD_PARTY_DIR}/spine)
include_directories(${THIRD_PARTY_DIR}/imgui/imgui)
include_directories(${THIRD_PARTY_DIR}/stb)

# global engine include
include_directories(${MAIN_SOURCE_DIR}/engine)

file(GLOB_RECURSE CPP_HEADERS ${MAIN_SOURCE_DIR}/*.hpp)
file(GLOB_RECURSE CPP_HEADERS2 ${MAIN_SOURCE_DIR}/*.h)
file(GLOB_RECURSE CPP_SOURCES ${MAIN_SOURCE_DIR}/*.cpp)


file(GLOB_RECURSE SOLOUD_FILES
    "../3rd/soloud/src/core/soloud_audiosource.cpp"
    "../3rd/soloud/src/core/soloud_bus.cpp"
    "../3rd/soloud/src/core/soloud_core_3d.cpp"
    "../3rd/soloud/src/core/soloud_core_basicops.cpp"
    "../3rd/soloud/src/core/soloud_core_faderops.cpp"
    "../3rd/soloud/src/core/soloud_core_filterops.cpp"
    "../3rd/soloud/src/core/soloud_core_getters.cpp"
    "../3rd/soloud/src/core/soloud_core_setters.cpp"
    "../3rd/soloud/src/core/soloud_core_voicegroup.cpp"
    "../3rd/soloud/src/core/soloud_core_voiceops.cpp"
    "../3rd/soloud/src/core/soloud_fader.cpp"
    "../3rd/soloud/src/core/soloud_fft_lut.cpp"
    "../3rd/soloud/src/core/soloud_fft.cpp"
    "../3rd/soloud/src/core/soloud_file.cpp"
    "../3rd/soloud/src/core/soloud_filter.cpp"
    "../3rd/soloud/src/core/soloud_misc.cpp"
    "../3rd/soloud/src/core/soloud_queue.cpp"
    "../3rd/soloud/src/core/soloud_thread.cpp"
    "../3rd/soloud/src/core/soloud.cpp"
    "../3rd/soloud/src/audiosource/wav/dr_impl.cpp"
    "../3rd/soloud/src/audiosource/wav/soloud_wav.cpp"
    "../3rd/soloud/src/audiosource/wav/soloud_wavstream.cpp"
    "../3rd/soloud/src/audiosource/wav/stb_vorbis.c"
    #"../3rd/soloud/src/backend/opensles/soloud_opensles.cpp"
    #"../3rd/soloud/src/backend/sdl_static/soloud_sdl_static.cpp"
    "../3rd/soloud/src/backend/miniaudio/soloud_miniaudio.cpp"
    "../3rd/soloud/src/filter/soloud_bassboostfilter.cpp"
    "../3rd/soloud/src/filter/soloud_biquadresonantfilter.cpp"
    "../3rd/soloud/src/filter/soloud_dcremovalfilter.cpp"
    "../3rd/soloud/src/filter/soloud_duckfilter.cpp"
    "../3rd/soloud/src/filter/soloud_echofilter.cpp"
    "../3rd/soloud/src/filter/soloud_eqfilter.cpp"
    "../3rd/soloud/src/filter/soloud_fftfilter.cpp"
    "../3rd/soloud/src/filter/soloud_flangerfilter.cpp"
    "../3rd/soloud/src/filter/soloud_freeverbfilter.cpp"
    "../3rd/soloud/src/filter/soloud_lofifilter.cpp"
    "../3rd/soloud/src/filter/soloud_robotizefilter.cpp"
    "../3rd/soloud/src/filter/soloud_waveshaperfilter.cpp"
)
add_library(soloud SHARED ${SOLOUD_FILES})



file(GLOB_RECURSE IMGUI_FILES
     "../3rd/imgui/imgui/imconfig.h"
     "../3rd/imgui/imgui/imgui.cpp"
     "../3rd/imgui/imgui/imgui.h"
     "../3rd/imgui/imgui/imgui_draw.cpp"

     #GLFW
     #"../3rd/imgui/imgui/imgui_impl_glfw.cpp"
     #"../3rd/imgui/imgui/imgui_impl_glfw.h"
     #SDL2
     "../3rd/imgui/imgui/imgui_impl_sdl2.h"
     "../3rd/imgui/imgui/imgui_impl_sdl2.cpp"

     #OPENGL
     "../3rd/imgui/imgui/imgui_impl_opengl3.h"
     "../3rd/imgui/imgui/imgui_impl_opengl3.cpp"
     "../3rd/imgui/imgui/imgui_impl_opengl3_loader.h"
     #VULKAN
     # "3rd/imgui/imgui/imgui_impl_vulkan.h"
     # "3rd/imgui/imgui/imgui_impl_vulkan.cpp"
     # DX12
     #"3rd/imgui/imgui/imgui_impl_dx12.h"
     #"3rd/imgui/imgui/imgui_impl_dx12.cpp"
     #"3rd/imgui/imgui/imgui_impl_win32.h"
     #"3rd/imgui/imgui/imgui_impl_win32.cpp"

     "../3rd/imgui/imgui/imgui_internal.h"
     "../3rd/imgui/imgui/imgui_tables.cpp"
     "../3rd/imgui/imgui/imgui_widgets.cpp"
     "../3rd/imgui/imgui/imstb_rectpack.h"
     "../3rd/imgui/imgui/imstb_rectpack.h"
     "../3rd/imgui/imgui/imstb_textedit.h"
     "../3rd/imgui/imgui/imstb_truetype.h"
     "../3rd/imgui/imgui/misc/cpp/imgui_stdlib.h"
     "../3rd/imgui/imgui/misc/cpp/imgui_stdlib.cpp"

     "../3rd/imgui/IconFont/IconsFontAwesome5.h"
     "../3rd/imgui/imgui/GraphEditor.h"
     "../3rd/imgui/imgui/GraphEditor.cpp"
     "../3rd/imgui/imgui/ImCurveEdit.h"
     "../3rd/imgui/imgui/ImCurveEdit.cpp"
     "../3rd/imgui/imgui/ImGradient.h"
     "../3rd/imgui/imgui/ImGradient.cpp"
     # "src/3rd/imgui/imgui/ImGuizmo.h"
     # "src/3rd/imgui/imgui/ImGuizmo.cpp"
     # "src/3rd/imgui/imgui/ImSequencer.h"
     # "src/3rd/imgui/imgui/ImSequencer.cpp"
     "../3rd/imgui/imgui/ImZoomSlider.h"
)

file(GLOB_RECURSE SPINE_FILES
     "../3rd/spine/spine/*.h"
     "../3rd/spine/spine/*.cpp"
)

file(GLOB_RECURSE LUA_FILES
     "../3rd/lua/lapi.c"
     "../3rd/lua/lcode.c"
     "../3rd/lua/lctype.c"
     "../3rd/lua/ldebug.c"
     "../3rd/lua/ldo.c"
     "../3rd/lua/ldump.c"
     "../3rd/lua/lfunc.c"
     "../3rd/lua/lgc.c"
     "../3rd/lua/llex.c"
     "../3rd/lua/lmem.c"
     "../3rd/lua/lobject.c"
     "../3rd/lua/lopcodes.c"
     "../3rd/lua/lparser.c"
     "../3rd/lua/lstate.c"
     "../3rd/lua/lstring.c"
     "../3rd/lua/ltable.c"
     "../3rd/lua/ltm.c"
     "../3rd/lua/lundump.c"
     "../3rd/lua/lvm.c"
     "../3rd/lua/lzio.c"
     "../3rd/lua/lauxlib.c"
     "../3rd/lua/lbaselib.c"
     "../3rd/lua/lcorolib.c"
     "../3rd/lua/ldblib.c"
     "../3rd/lua/liolib.c"
     "../3rd/lua/lmathlib.c"
     "../3rd/lua/loadlib.c"
     "../3rd/lua/loslib.c"
     "../3rd/lua/lstrlib.c"
     "../3rd/lua/ltablib.c"
     "../3rd/lua/lutf8lib.c"
     "../3rd/lua/linit.c"
)
add_library(lua SHARED ${LUA_FILES})
add_library(imgui SHARED ${IMGUI_FILES})


# DEFINES
add_compile_definitions(NOMINMAX)
add_compile_definitions(USE_SDL)
add_compile_definitions(SOL_NO_EXCEPTIONS=1)
add_compile_definitions(USING_GLES)
# if (backend == opengl)
add_compile_definitions(OPENGL_BACKEND)
add_compile_definitions(OPENGL_SIMPLE_RENDER)

add_executable(
    cross_test
    ${CPP_HEADERS}
    ${CPP_HEADERS2}
    ${CPP_SOURCES}
    ${SPINE_FILES}
)

target_link_libraries(cross_test ${CMAKE_CURRENT_SOURCE_DIR}/lib/libassimp.a)
target_link_libraries(cross_test ${CMAKE_CURRENT_SOURCE_DIR}/lib/libzlibstatic.a)
target_link_libraries(cross_test soloud)
target_link_libraries(cross_test lua)
target_link_libraries(cross_test imgui)

list(APPEND PLATFORM_OPTIONS
    "-pthread"
)

target_compile_options(
    cross_test
    PUBLIC 
    ${PLATFORM_OPTIONS}
)

set_target_properties(
    cross_test
    PROPERTIES
    LINK_FLAGS
    "-O2 -s EXPORTED_FUNCTIONS=\"['_malloc','_main']\" -s EXPORTED_RUNTIME_METHODS=\"['ccall', 'cwrap']\"  -s USE_SDL=2 -s USE_SDL_IMAGE=2 -D WITH_MINIAUDIO -s SDL2_IMAGE_FORMATS=\"['png']\" -s FULL_ES3=1 -s USE_WEBGL2=1 -s WASM=1 -s MIN_WEBGL_VERSION=2 -s GL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=1 -s OFFSCREEN_FRAMEBUFFER=1 -s STACK_SIZE=8MB -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=64MB -s ASSERTIONS  -s USE_FREETYPE=1 --preload-file ../../assetses@/assets"
)
