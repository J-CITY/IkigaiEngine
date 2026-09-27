Install SDL

- Win
Все либы в 3rd папку
Download https://www.libsdl.org/release/SDL2-devel-2.0.9-VC.zip
Extract to ./3rd/SDL_windows folder

Download GLEW https://github.com/nigels-com/glew/releases/download/glew-2.1.0/glew-2.1.0-win32.zip
Extract to ./3rd/glew

# generate VS project
cmake -G "Visual Studio 17 2022" ./ -Bbuild

# скопировать dll в windows\out\
--------------------------------------------------------------
- Android

- copy SDL SDL_image to android\sdl\jni
- repace in  android\sdl\jni\SDL2_image\Android.mk: SUPPORT_WEBP ?= true to SUPPORT_WEBP ?= false
- copy assets folder to android\app\src\main\assets
- copy 3rd\SDL\android-project\app\src\main\java to android\sdl\src\main
- copy 3rd\SDL to android\sdl\jni

- В консоле Android Studio вызвать: ./gradlew :app:assembleDebug

- Build ASSIMP
Patch in assimp - ioapi.h #if defined(USE_FILE32API) -> #if defined(USE_FILE32API) || defined(ANDROID) 

Patch in Assimp - port/AndroidJNI/CMakeLists.txt
-INSTALL(TARGETS android_jniiosystem EXPORT "${TARGETS_EXPORT_NAME}")
+INSTALL(TARGETS android_jniiosystem EXPORT "${TARGETS_EXPORT_NAME}"
+       RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
+       LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
+               ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

glm patch
setup.hpp:143 #if GLM_PLATFORM == GLM_PLATFORM_ANDROI && !defined(GLM_LANG_STL11_FORCED)

freetype patch

CMakeList.txt:305
add_compile_definitions(HAVE_UNISTD_H)
add_compile_definitions(HAVE_FCNTL_H)

cmake -DCMAKE_TOOLCHAIN_FILE=C:\Users\DaniilGlushchenko\AppData\Local\Android\Sdk\ndk\26.0.10792818\build\cmake\android.toolchain.cmake -DANDROID_NDK=C:/Users/DaniilGlushchenko/AppData/Local/Android/Sdk/ndk/26.0.10792818 -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI="arm64-v8a" -DASSIMP_ANDROID_JNIIOSYSTEM=ON DBUILD_SHARED_LIBS=1 . -B build -G Ninja
cmake --build .


---------------------------------------------------------------
- Emscripten
emcmake cmake . -B build -G Ninja
cmake --build build

Run local server
python -m http.server 8000

- Emscripten Assimp
Download Assimp Release 5.0.1 (https://github.com/assimp/assimp/releases/tag/v5.0.1)
Run the following commands after installing Emscripten (https://emscripten.org/docs/getting_started/downloads.html):
cd assimp-5.0.1
emcmake cmake -DCMAKE_BUILD_TYPE=Release . 
emmake make
cd ..
Link the static libraries in your CMake project:
include_directories("${PROJECT_SOURCE_DIR}/assimp-5.0.1/include")
link_libraries("${PROJECT_SOURCE_DIR}/assimp-5.0.1/lib/libassimp.a")
link_libraries("${PROJECT_SOURCE_DIR}/assimp-5.0.1/lib/libIrrXML.a")
link_libraries("${PROJECT_SOURCE_DIR}/assimp-5.0.1/lib/libzlib.a")



//TODO: set canvas size

//Create link
New-Item -ItemType SymbolicLink -Path ".\windows\buildVulkan\assets" -Target ".\assets"

-------------------------------
imgui patch

ADD global define IMGUI_IMPL_VULKAN_NO_PROTOTYPES

imgui_impl_vulkan.cpp

vkCmdPipelineBarrier(fd->CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
to
vkCmdPipelineBarrier(fd->CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(loader_func("vkCmdBeginRendering", user_data));
ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(loader_func("vkCmdEndRendering", user_data));
--------------------------


macOS
brew install sdl2 glew vulkan-headers vulkan-loader
cmake -B mac/build -S mac


cmake --build mac/build -j 8

xcode
# Генерируем проект для Xcode в папку mac/build_xcode
cmake -G Xcode -B mac/build_xcode -S mac

# Открываем созданный проект в Xcode
open mac/build_xcode/IkigaiEngine.xcodeproj
