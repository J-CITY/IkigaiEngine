- Assets
Локальный каталог assets/ не в Git. После clone run_vs.py / setup_env.py
сами делают pull, если локальная копия старше utils/assets.source.json
или каталога нет:

python utils/sync_assets.py
python utils/sync_assets.py ensure

Подробности: docs/ASSETS.md
--------------------------------------------------------------
- Win
1. Подготовка окружения (скачивание зависимостей и сабмодулей).
   run_vs.py вызывает setup_env.py сам, если окружение ещё не готово:
python run_vs.py -c vs22 -a x64 -g opengl -t Release
# или
python run_vs.py -c vs22 -a x64 -g vulkan -t Debug
# или
python run_vs.py -c vs22 -a x64 -g dx12 --build
# несколько API в одном exe; активный выбирается при запуске
python run_vs.py -c vs22 -a x64 -g opengl vulkan dx12 --build
IkigaiEngine.exe --render-backend=vulkan

--use-editor / --use_file_watcher — редактор и hot-reload (CMake USE_EDITOR / USE_FILE_WATCHER):
python run_vs.py --use-editor 0
python run_vs.py --use_file_watcher 0
python run_vs.py --use_file_watcher 1

Лог cmake/сборки пишется в run_vs.log (переопределить: --log path)

Ручной вызов:
python utils/setup_env.py
--------------------------------------------------------------
- Android
Open android/out in Android Studio (not repo root). After changing app id, Sync Gradle and uninstall old com.daniil.cross_test.
python run_vs.py -p android
python run_vs.py -p android --esVer 3.1 --abi arm64-v8a --build
--------------------------------------------------------------
- Meta Quest
Open quest/out in Android Studio (not repo root). Headset must be in developer mode.
NativeActivity + OpenXR GLES, no SDL, no editor. ABI by default: arm64-v8a.
run_vs.py pulls 3rd/OpenXR-SDK and fetches the Android loader into 3rd/MetaOpenXR/ if needed.

python run_vs.py -p quest
python run_vs.py -p quest --abi arm64-v8a --build

# loader only, if Maven is blocked set META_OPENXR_AAR to a local .aar:
python utils/fetch_meta_openxr.py
python utils/fetch_meta_openxr.py --force

Do not edit quest/out by hand. Change platform/quest/templates or cmake/IkigaiEngineQuest.cmake, then regenerate.
---------------------------------------------------------------
- Emscripten
Нужны Python 3, Git, CMake и Ninja. Emscripten SDK 6.0.10 ставится из submodule 3rd/emsdk
при первом запуске (версия в emscripten/emsdk.version).

python run_vs.py -p web -t Release --build
python run_vs.py -p web --run

# или напрямую из SDK:
3rd\emsdk\upstream\emscripten\emrun.exe emscripten\out\IkigaiEngine.html

# или
python -m http.server 8000 --directory emscripten/out
# открыть http://localhost:8000/IkigaiEngine.html
---------------------------------------------------------------
macOS
brew install glew vulkan-headers vulkan-loader molten-vk

# -p macos и -p mac — одно и то же. -g задаёт API, которые компилируются
# в один IkigaiEngine.app (по умолчанию mac/build, бинарник mac/out).
# Активный API при запуске: assets/engine/Configs/render.json
# (если есть assets/game/Configs/render.json, он перекрывает engine)
# или аргумент --render-backend=opengl|vulkan|metal.

python run_vs.py -p macos -g opengl --build
python run_vs.py -p macos -g opengl vulkan metal --build
open mac/out/IkigaiEngine.app --args --render-backend=metal
open mac/out/IkigaiEngine.app --args --render-backend=vulkan

OpenGL: SDL core 3.2 + GLEW (Homebrew). Apple deprecation warnings are expected.
Vulkan: needs MoltenVK at runtime (libMoltenVK / vulkan-loader). Debug builds
also ask for VK_LAYER_KHRONOS_validation; use Release if that layer is missing.
Metal: SDL_WINDOW_METAL and a CAMetalLayer; no extra brew package.

# Xcode project (default dir mac/build_xcode, binary still mac/out)
# After changing mac/CMakeLists.txt, re-run configure so Xcode picks up sources/scripts.
python run_vs.py -p macos -c xcode -g opengl vulkan metal
open mac/build_xcode/IkigaiEngine.xcodeproj
python run_vs.py -p macos -c xcode -g opengl vulkan metal --build
