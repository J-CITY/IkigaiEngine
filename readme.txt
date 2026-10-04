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
brew install sdl2 glew vulkan-headers vulkan-loader
cmake -B mac/build -S mac

cmake --build mac/build -j 8

xcode
# Генерируем проект для Xcode в папку mac/build_xcode
cmake -G Xcode -B mac/build_xcode -S mac

# Открываем созданный проект в Xcode
open mac/build_xcode/IkigaiEngine.xcodeproj
