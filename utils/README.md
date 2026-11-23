# Утилиты `utils/`

Каталог `utils/` — инструменты вокруг движка: подготовка машины, синхронизация контента, генерация проектов и проверки. Сюда не входит рантайм. Игру и редактор собирает CMake из `src/`; тесты и бенчмарки живут в `testModule/`. Production-таргет собирается без `utils/tests` и без `testModule/`.

Запускайте скрипты из корня репозитория через лаунчер. Он ставит [uv](https://docs.astral.sh/uv/), берёт интерпретатор Python 3.12 из локального `.python/` и зависимости из `uv.lock`, не трогая пользовательский `PATH` и чужой virtualenv.

```bat
run.cmd utils/setup_env.py
run.cmd utils/assets/sync_assets.py ensure
run.cmd --content python -c "import PIL"
run.cmd --tests python -c "import jsonschema, numpy"
```

На Linux и macOS то же самое делает `sh run.sh ...`. Флаг `--content` добавляет extra `content` (Pillow). Флаг `--tests` добавляет группу `tests` (`jsonschema`, `numpy`, Pillow) и тот же extra. Обычные команды окружения и синхронизации обходятся базовыми зависимостями (`gdown`, `certifi`).

`setup_env.py` и `sync_assets.py` при прямом запуске `python` сами перезапускаются через лаунчер, кроме `--help`. Остальные скрипты этого не делают: их вызывает уже подготовленный `run_vs.py` или CMake. Для ручного запуска берите `run.cmd` / `run.sh`.

Скачанные SDK, кэш uv и zip-архивы контента пишутся в `utils/.pack/`. Этот каталог в `.gitignore`. В git остаются только скрипты и маленькие указатели вроде `assets.source.json`.

## Карта

| Путь | Когда нужен |
| --- | --- |
| `setup_env.py` | Первый запуск и смена платформы. Его же вызывает `run_vs.py`, если окружение ещё не готово. |
| `assets.source.json` | Указатель на публичную папку Google Drive с игровым контентом. Его коммитят, байты ассетов — нет. |
| `assets/` | Скачивание контента и сборка каталога, который уезжает в билд. |
| `environment/` | Python, системные пакеты, GLEW, Spine, Emscripten, OpenXR loader, DLSS/FSR/Streamline. |
| `build/` | Reflection, Gradle-проекты Android и Quest, проверка слоёв `#include`, патч FSR под Vulkan. |
| `tools/` | Форматирование, граф модулей, картинки бенчмарков. В конфигурацию CMake не входят. |
| `tests/` | Локальные тестовые скрипты. Каталог в `.gitignore`, в репозиторий его не кладут. |

`run_vs.py` в корне — оркестратор сборки. Он читает пресеты, зовёт `setup_env.py` и платформенные генераторы. Ниже описаны только файлы внутри `utils/`.

## Подготовка окружения

```bat
run.cmd utils/setup_env.py
run.cmd utils/setup_env.py quest
run.cmd utils/setup_env.py win -g vulkan dx12 --dlss-sr --fsr-sr --dlss-fg --fsr-fg
```

Позиционный аргумент: `win` (по умолчанию), `mac`, `linux`, `ios`, `web`, `android`, `quest`. `-g` задаёт графические API, которые попадут в один бинарник: `opengl`, `vulkan`, `dx12`, `metal`. `--generator` нужен сборке патченого FSR Vulkan и по умолчанию равен `Visual Studio 17 2022`.

Общий порядок `main`:

1. `git submodule update --init --recursive` для базовых сабмодулей.
2. Платформенные пакеты: GLEW на Windows, Homebrew на macOS, apt или pacman на Linux, Android OpenXR loader на Quest.
3. Spine Runtime 4.2 в `3rd/spine/spine`.
4. Выбранные SR/FG SDK, если переданы флаги.
5. `ensure` для игровых ассетов.

Общий `submodule update` инициализирует базовые сабмодули. Пути `3rd/DLSS`, `3rd/FidelityFX-Vulkan`, `3rd/FSR-DX12` и `3rd/Streamline` из этого списка исключены: их инициализируют отдельно, когда нужны SR/FG. `environment/sdks.py` после этого только проверяет, что checkout стоит на записанном pin и что нужные заголовки и DLL на месте. Обычная сборка OpenGL поэтому не тянет гигабайты NVIDIA/AMD SDK.

Флаги SR/FG принимает и `setup_env.py`, и `run_vs.py`. Они описаны в `environment/render_sdk_options.py`:

| Флаг | CMake | Что готовится |
| --- | --- | --- |
| `--dlss-sr` | `USE_DLSS` | Сабмодуль DLSS. |
| `--fsr-sr` | `USE_FSR` | FSR для выбранных `vulkan` и/или `dx12`. |
| `--dlss-fg` | `USE_DLSS_FRAME_GENERATION` и `USE_STREAMLINE` | Frame generation через Streamline. |
| `--fsr-fg` | `USE_FSR_FRAME_GENERATION` | FSR frame generation. Для Vulkan ещё и патченый runtime. |

Ограничение зашито в `RenderSdkOptions.validate`: эти SDK собираются для `-p win`, архитектуры x64 и хотя бы одного бэкенда из `vulkan` и `dx12`. У вендорных библиотек OpenGL-пути нет, поэтому флаг при одном `-g opengl` отклоняется.

`is_environment_ready` смотрит маркер `3rd/assimp/CMakeLists.txt`, версию Spine, GLEW на Windows, пакеты brew/apt/pacman и готовность выбранных SR/FG. `run_vs.py` по этому предикату решает, звать ли `setup_env.py`.

### `environment/`

| Файл | Роль |
| --- | --- |
| `python_env.ps1`, `python_env.py` | Точка входа лаунчера. `run.cmd` вызывает ps1. `ensure_project_python()` перезапускает скрипт, если `IKIGAI_PROJECT_PYTHON` ещё не выставлен. |
| `http_download.py` | Скачивание по URL. Сначала `urllib` с CA из `certifi`, на Windows при ошибке сертификата — `curl.exe`. |
| `update_glew.py` | GLEW 2.3.1, zip с GitHub, распаковка в `3rd/glew`. Каталог в `.gitignore`: это бинарный Windows-пакет, а не исходники, которые имеет смысл диффить. Повторный запуск сверяет `version.txt`. |
| `update_spine.py` | Ветка `4.2` spine-runtimes. Кладёт рантайм в `3rd/spine/spine` и пишет `version.txt`. Вызывается на каждой платформе. |
| `ensure_emsdk.py` | Активирует Emscripten из сабмодуля `3rd/emsdk`. Версия читается из `platform/web/emsdk.version`. `run_vs.py -p web` вызывает `ensure_emsdk()` и потом `emrun_path()`. Прямой запуск: `run.cmd utils/environment/ensure_emsdk.py`. |
| `fetch_meta_openxr.py` | Android-загрузчик Khronos `openxr_loader_for_android` в `3rd/MetaOpenXR`. Версия и URL — в `platform/quest/meta_openxr_version.json`. Заголовки остаются в сабмодуле `3rd/OpenXR-SDK`. `--force` качает заново. Если переменная `META_OPENXR_AAR` указывает на локальный `.aar`, скрипт копирует его и не ходит по URL. |
| `sdks.py` | Read-only проверка pin и состава DLSS, FSR Vulkan, FSR DX12, Streamline. Сам сабмодуль не обновляет. |
| `streamline.py` | Официальный runtime Streamline 2.14.1 (zip с GitHub, SHA-256 зафиксирован) в `utils/.pack/streamline-2.14.1`. Заголовки берутся из сабмодуля, DLL — из этого архива. |
| `render_sdks.py` | Склеивает проверку сабмодулей, установку Streamline и сборку патченого FSR Vulkan. |
| `render_sdk_options.py` | Общие флаги для `setup_env.py` и `run_vs.py`, плюс список `-D` для CMake. |

Пакеты, которые ставит `setup_env.py` сам:

- macOS, Homebrew: `glew`, `vulkan-headers`, `vulkan-loader`, `molten-vk`. Без `brew` скрипт останавливается.
- Debian/Ubuntu: cmake, ninja, компилятор, OpenGL, GLEW, Vulkan, X11, Wayland, PulseAudio, ALSA. Ставится через `sudo apt-get install`.
- Arch: тот же набор через `sudo pacman -S`.

На Linux скрипт запрашивает пароль. Если ни `apt-get`, ни `pacman` нет, он печатает предупреждение и идёт дальше: дистрибутив в этом случае на совести машины.

## Контент

Игровые байты не лежат в git. В репозитории есть `assets/engine` (то, без чего не стартует ни одна платформа) и `assets_packs/*/pack.json`. Тяжёлые демо и `assets/game` приезжают с Google Drive. `utils/assets.source.json` — коммитируемый указатель: режим, id папки, имя zip, sha256, версия, локальный путь (`assets`).

Локальное дерево помечается файлом `assets/.ikigai-assets-stamp.json`. `ensure` сравнивает штамп с `assets.source.json`:

| Состояние | Что делает `ensure` |
| --- | --- |
| `missing` | Каталога нет или в нём нет файлов. Скачивает. |
| `stale` | В источнике версия или хеш новее штампа, либо штампа нет, а в источнике уже есть версия. Скачивает. |
| `current` | Штамп совпал. Ничего не качает. |
| `local-newer` | Локальная версия того же источника больше. Не затирает не опубликованную работу. |
| `unmarked` | Файлы есть, штампа нет, и источник ещё без версии и хеша. Пишет штамп, не скачивая. |

```bat
run.cmd utils/assets/sync_assets.py
run.cmd utils/assets/sync_assets.py ensure
run.cmd utils/assets/sync_assets.py pull-mirror
run.cmd utils/assets/sync_assets.py pack
run.cmd utils/assets/sync_assets.py pack --use-zip-pull
run.cmd utils/assets/sync_assets.py push
run.cmd utils/assets/sync_assets.py push-mirror
```

Команды:

- `pull` (действие по умолчанию). В режиме `folder` качает публичную папку через `gdown`, аккаунт Google не нужен. В режиме `zip` качает файл по `driveFileId` и сверяет SHA-256.
- `pull-mirror`. То же, но удаляет локальные файлы, которых нет в источнике.
- `ensure`. Тянет только `missing` и `stale`. Это путь `setup_env.py` и `run_vs.py`.
- `pack`. Собирает zip в `utils/.pack/` из `assets/game` и байтов паков `demo_*` (`pack.json` в архив не входит: он уже в git). Версия в `assets.source.json` увеличивается на 1, туда же пишется sha256. Файлы `.vmesh` кладутся без сжатия, чтобы страницы геометрии оставались адресуемыми внутри архива. `--use-zip-pull` переключает `mode` на `zip`.
- `push` / `push-mirror`. Нужен `rclone` и один раз `rclone config` на этой машине. OAuth остаётся в локальном конфиге rclone. Имя remote берётся из переменной `IKIGAI_RCLONE_REMOTE` или из `utils/assets.remote.json`:

```json
{ "rcloneRemote": "gdrive", "rclonePath": "" }
```

Файл описывает машину автора контента. В коммит его не кладут. Если remote ровно один, имя можно не указывать. `push-mirror` делает `rclone sync` и удаляет на Drive то, чего нет локально.

`--dry-run` печатает план и ничего не пишет.

После `pack` и zip-`push` в git коммитят обновлённый `utils/assets.source.json`. Сам zip остаётся в `utils/.pack/`.

Почему два режима. `folder` удобен, пока контент правят руками на Drive. `zip` даёт один файл, версию и хеш: pull либо целиком совпал, либо нет. Пока `driveFileId` пустой, `mode` должен оставаться `folder`.

`assets/content_packs.py` делает другую работу: не скачивает, а собирает дерево, которое увидит конкретная платформа. CMake зовёт его на iOS и Web. Вручную:

```bat
run.cmd utils/assets/content_packs.py stage --platform windows --layout repo --dest build/staged-windows
run.cmd utils/assets/content_packs.py stage --platform android --layout apk --dest build/staged-android
```

Платформы: `windows`, `linux`, `macos`, `ios`, `web`, `android`, `quest`.

Пак выбирается, если в его `pack.json` поле `platforms` содержит эту платформу или `"*"`. `requires` подтягивается рекурсивно; цикл в зависимостях — ошибка. Имена на выходе сортируются: нативный код и Web монтируют паки в алфавитном порядке, и стадия должна повторять этот порядок.

- `layout=repo` кладёт `assets/engine`, `assets/game` и выбранные паки как `assets_packs/<id>`.
- `layout=apk` кладёт те же `engine` и `game` в корень стадии, паки в `packs/`, плюс `content_packs.json` со списком id.

Копируются обычные файлы. Symlink и junction запрещены: стадия не должна утащить в APK или в веб-пакет путь с машины разработчика. Назначение не должно пересекаться с `assets/` и `assets_packs/`. Каталог назначения либо пустой, либо уже помечен `.ikigai-content-stage`. Запись идёт во временный каталог рядом и публикуется переименованием, чтобы оборванная копия не заменила прошлую удачную стадию.

`merge_drive_payload` и `iter_drive_archive` экспортируются для `sync_assets.py`. Они знают старую раскладку Drive, где демо ещё лежали внутри `game/`, и раскладывают их по `assets_packs/demo_*`. Файл из `local/asset-archive/` не возвращается в рабочее дерево, пока его снова не положат на исходный путь.

## Сборка

Эти скрипты вызываются из CMake и `run_vs.py`. Ручной запуск нужен, когда отлаживаете генерацию, а не когда собираете игру.

### Reflection

`build/header_parser.py` читает узкий язык аннотаций. Это не препроцессор и не libclang: макросы не раскрываются, `#if` не выбирает ветку. Лексер понимает комментарии, строковые литералы и вложенные скобки. Неподдержанный объявитель — ошибка, а не «тихий» пропуск. Так сгенерированный реестр не зависит от того, какие макросы определены на конкретной платформе.

Аннотации, которые должны стоять непосредственно перед объявлением:

- `IKI_CLASS(...)` перед `class` или `struct`;
- `IKI_ENUM(...)` перед `enum` / `enum class`;
- `IKI_PROPERTY(...)` и `IKI_FUNCTION(...)` внутри уже помеченного класса.

У вложенного класса `Name` обязан совпадать с C++-именем. У свойства без явных `Name` и `Type` парсер берёт последнее имя и тип из объявления; указатели на функции и массивы так не разбираются, для них нужны оба поля.

`build/IkigaiHeaderTool.py` обходит заголовки и пишет `*.generated.h`, `*.generated.cpp` и `reflectionRegistry.generated.cpp`. Первая строка сгенерированного файла — `// AUTO GENERATED FILE. DO NOT MODIFY.` Файлы с этой меткой, которым больше нет исходного заголовка, удаляются. Часть компонентов оборачивается в `#if USE_LUA`, `USE_IKIGAI_SCRIPT`, `USE_PHYSICS_2D`, `USE_PHYSICS_3D`, чтобы выключенная фича не попадала в линковку.

CMake (`cmake/Codegen.cmake`) на конфигурации вызывает:

```text
python utils/build/IkigaiHeaderTool.py <корень>/src <binary>/generated --list-outputs
```

`--list-outputs` печатает манифест и ничего не генерирует. Сама генерация идёт отдельным шагом сборки в `${CMAKE_BINARY_DIR}/generated`. Имена отражённых заголовков должны быть уникальны без пути: выход называется по basename.

Ручной прогон:

```bat
run.cmd utils/build/IkigaiHeaderTool.py src build/reflection-preview
```

### Android и Quest

`run_vs.py -p android` и `-p quest` вызывают генераторы. Проекты появляются в `build/android` и `build/quest`. Их не правят руками: шаблоны лежат в `platform/android/templates` и `platform/quest/templates`, логика Gradle/CMake — в `cmake/`. После правки шаблона проект генерируют заново.

```bat
run.cmd utils/build/generate_android.py --esVer 3.2 --abi arm64-v8a,armeabi-v7a
run.cmd utils/build/generate_quest.py --abi arm64-v8a
```

Общие флаги: `-o` / `--output` (куда положить Gradle-проект) и `--artifacts-output` (база для APK, конфигурация дописывается сама).

Android. SDL3 собирается из `3rd/SDL3`, Java-обвязка `org.libsdl.app` каждый раз копируется из `3rd/SDL3/android-project`. OpenGL ES:

| `--esVer` | minSdk | Почему такой порог |
| --- | --- | --- |
| `3`, `3.1` | 21 | Ниже SDL3 Android не собирается, хотя ES 3.0 бывает и раньше. |
| `3.2` (по умолчанию) | 24 | ES 3.2 требует API 24. |

`compileSdk` и `targetSdk` — 35. ABI по умолчанию: `arm64-v8a,armeabi-v7a`. Application id по умолчанию: `com.ikigai.engine`. Свой id должен совпасть с `kMainActivityJniClass` в `androidStorage.cpp`, иначе JNI не найдёт activity. Нужен JDK 17 или новее: берётся `JAVA_HOME`, если версия подходит, иначе JBR из Android Studio.

Quest. Отдельное приложение NativeActivity + OpenXR GLES 3.2, без SDL и без редактора. minSdk 29, ABI по умолчанию `arm64-v8a`. Генератор сам вызывает `fetch_meta_openxr`, если загрузчика ещё нет. В Android Studio открывают `build/quest`, не корень репозитория. Шлем должен быть в developer mode.

`build/gradle_cmake.py` переводит опции движка в `-D` для Gradle. Ключи, которыми владеет Android Gradle Plugin (toolchain, ABI, NDK, тип сборки, компиляторы, каталоги вывода), сюда не попадают: иначе AGP и скрипт спорят за одну и ту же переменную, и победитель зависит от порядка. Имена переменных проверяются по шаблону CMake, значения экранируются для Groovy.

`build/cmake_presets.py` читает `CMakePresets.json` и, если он есть, `CMakeUserPresets.json` для лаунчера: наследование, include, макросы `$penv{...}`, относительный binary dir от корня репозитория. Условия пресетов и дефолты фич движка он не вычисляет. Доступность пресета всё равно подтверждает сам CMake. Незнакомый макрос — ошибка, а не пустая строка.

### Слои `#include`

```bat
run.cmd utils/build/check_include_layers.py
run.cmd utils/build/check_include_layers.py --update
```

Скрипт обходит `src/engine` и ищет `#include`, который ведёт из модуля более низкого слоя в более высокий. Разрешённые рёбра записаны в `build/include_layers_baseline.txt`. Новое ребро завершает процесс с кодом 1. `--update` переписывает baseline после осознанного изменения слоёв.

Слои в `LAYERS`:

| Слой | Модули |
| --- | --- |
| 0 | `utilsModule`, `taskModule`, `coroutineModule`, `mathModule` |
| 1 | `collisionModule`, `resourceModule`, `windowModule` |
| 2 | `ecsModule`, `coreModule`, `physicsModule`, `audioModule`, `scriptModule`, `skeletalModule` |
| 3 | `sceneModule` |
| 4 | `renderModule` |
| 5 | `editorModule` |

Модуля нет в таблице — он считается слоем 2. Проверка поэтому строгая только там, где слой назван явно. Include внутри одного модуля и include на тот же слой не записываются. Сторонние заголовки, которые не резолвятся внутрь `src/engine`, пропускаются.

В конфигурацию CMake этот скрипт не подключён. Его запускают перед коммитом, когда двигают зависимости между модулями.

### FSR Vulkan

```bat
run.cmd utils/build/fidelityfx_vulkan.py
run.cmd utils/build/fidelityfx_vulkan.py --generator "Visual Studio 17 2022" --parallel 3
```

Сабмодуль `3rd/FidelityFX-Vulkan` не патчится. Скрипт копирует исходники закреплённого коммита в `utils/.pack/fidelityfx-vulkan-patched`, накладывает правки (формат luma history, размер пула storage-buffer дескрипторов, семафор present на кадр, очистка ресурсов swapchain) и собирает `amd_fidelityfx_vk.dll`. Манифест рядом хранит pin, список патчей и sha256 DLL. Повторный запуск ничего не пересобирает, пока манифест совпадает.

Нужен Windows Vulkan SDK: переменная `VULKAN_SDK` или `glslangValidator` в `PATH`, либо установленный `C:\VulkanSDK`. `--reuse-source` оставляет уже подготовленные исходники с тем же pin. Если pin сабмодуля разъехался с константой в скрипте, сборка останавливается: патчи привязаны к конкретному тексту.

`setup_env.py` вызывает `ensure_runtime()` сам, когда запрошены `--fsr-sr` или `--fsr-fg` вместе с `-g vulkan`.

## Ручные инструменты

Они не участвуют в `run_vs.py`. Зависимости `networkx`, `pyvis` и `matplotlib` в `pyproject.toml` нет: это разовые пакеты, их ставят в окружение отдельно.

### Форматирование

```bat
run.cmd utils/tools/format_code.py
run.cmd utils/tools/format_code.py src/engine/ecsModule
```

Без аргумента обходится корень репозитория. Ищет `.cpp`, `.hpp`, `.h`, `.c`, `.cc`, `.cxx` и гоняет `clang-format -i -style=file` по `.clang-format` репозитория. Не заходит в `build`, `3rd`, `vendor`, `.git`, `out`, `generated`. `clang-format` должен быть в `PATH`. Для обычной правки надёжнее форматировать только изменённые файлы: скрипт переписывает всё дерево, и diff тонет в чужом стиле.

### Граф модулей

```bat
run.cmd utils/tools/analyze_module_graph.py
```

Считает `#include` между каталогами `src/engine/*` и библиотеками в `3rd/`. Комментарии, литералы и raw-строки не считаются директивами. Печатает таблицу «кто от кого зависит» и ищет циклы по сильным компонентам связности среди модулей движка. HTML пишется в `out/engine_dependencies.html` относительно текущего каталога. Запускайте из корня: и `out/`, и `utils/tools/out` в `.gitignore`.

Зелёные узлы — модули движка, оранжевые — третьи стороны. Толщина ребра растёт с числом включений. Клик по узлу красит входящие и исходящие рёбра; циклические рисуются пунктиром.

Это обзор архитектуры, не проверка слоёв. Запрет новых рёбер вверх делает `check_include_layers.py`.

### Графики бенчмарков

```bat
run.cmd utils/tools/generate_charts.py --theme light
run.cmd utils/tools/generate_charts.py --theme dark --format png svg
run.cmd utils/tools/generate_charts.py --list
```

Четыре графика из медиан в `testModule/benchmarks/ecs`. Числа зашиты в `CHARTS` внутри скрипта, бенчмарк заново не гоняется. Подписи на русском, поэтому шрифт должен содержать кириллицу (`--font`, по умолчанию DejaVu Sans).

Полезные флаги: `--charts` (имена из `--list`), `--theme` (`light`, `dark`, `paper`), `--output-dir` (иначе `build/ecs-charts/<theme>`), `--format png|svg|pdf`, `--dpi`, `--figsize WIDTH HEIGHT`, `--font-size`, `--colors` из четырёх цветов (baseline, Ikigai, EnTT, Flecs), `--mpl-style`.

Нужен Python 3.10+ и matplotlib. У проекта зафиксирован 3.12, так что версия лаунчера подходит; matplotlib ставят отдельно.

## Что куда класть

- Постоянный инструмент сборки или контента — в `assets/`, `environment/` или `build/`, и его вызывает `setup_env.py`, `run_vs.py` или CMake.
- Разовая проверка и локальный скрипт эксперимента — в `utils/tests/`. Каталог не в git. Постоянный тест оформляют отдельной задачей и кладут в `testModule/` или в отслеживаемый скрипт, а не в этот каталог.
- Скачанный или собранный бинарник — в `utils/.pack/` или в уже игнорируемый `3rd/<имя>/`. В исходники сабмодуля патчи не вносят: сабмодуль остаётся чужим pin, правка живёт в скрипте, который копирует дерево в `.pack`.
- Виртуальные пути движка (`/shaders/...`, `/materials/...`) в эти скрипты не протаскивают имя пака. Паки монтируются в корень, стадия только выбирает, какие каталоги скопировать.
