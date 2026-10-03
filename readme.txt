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
//TODO
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
