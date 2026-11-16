# ECS benchmarks

[Сравнение Ikigai, EnTT и Flecs](RESULTS.md)

Адаптер Ikigai в сравнительной матрице создаёт мир с `ArchetypeLayout::SharedRows`. Для архетипных компонентов этот экспериментальный режим требует `noexcept` у конструктора перемещения, присваивания перемещением и деструктора. Обычный `World()` использует `ArchetypeLayout::PerColumn`.

Сборка использует три локальных checkout в каталоге рядом с движком. Ниже команды PowerShell из корня IkigaiEngine.

```powershell
$deps = Join-Path (Split-Path -Parent $PWD.Path) 'ikigai-ecs-deps'
New-Item -ItemType Directory -Path $deps -Force | Out-Null
git clone --depth 1 --branch v3.15.0 https://github.com/skypjack/entt.git "$deps/entt"
git clone --depth 1 --branch v4.1.2 https://github.com/SanderMertens/flecs.git "$deps/flecs"
git clone --depth 1 --branch v1.9.4 https://github.com/google/benchmark.git "$deps/benchmark"

cmake -S testModule -B build/ecs -G 'Visual Studio 17 2022' -A x64 "-DIKIGAI_ECS_BENCHMARK_DEPS=$deps" -DIKIGAI_BUILD_ECS_BENCHMARKS=ON
cmake --build build/ecs --config Release --target ecs_benchmarks
$exe = './build/ecs/benchmarks/ecs/Release/ecs_benchmarks.exe'
$filter = '^(movement<(IkigaiCompact|IkigaiMixed|EnTT|FlecsUncached|FlecsCached)>|structural<(IkigaiCompact|EnTT|FlecsUncached),)'
& $exe "--benchmark_filter=$filter" --benchmark_min_time=0.05s --benchmark_min_warmup_time=0.01 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/ecs-comparison.json --benchmark_out_format=json
```

| Репозиторий | SHA |
|---|---|
| EnTT | `d4014c74dc3793aba95ae354d6e23a026c2796db` |
| Flecs | `a53b4715c0b91e366bbfce53d9abd2b61534f7a3` |
| Google Benchmark | `eddb0241389718a23a42db6af5f0164b6e0139af` |

## Как читать результаты

- `movement` измеряет проход по сущностям. У всех ECS одинаковые позиция, скорость, шаг и доля совпадений. При 16 наборах компонентов четыре дополнительных тега задают все 16 комбинаций. EnTT использует `view`. Flecs измеряется с `QueryCacheNone` и `QueryCacheAuto`.
- `IkigaiMixed` использует те же данные и ту же операцию при 10% и 50% совпадений. Скорость хранится в sparse-пуле.
- `structural` измеряет создание и удаление пустых сущностей, добавление, снятие и цикл добавления/снятия компонента у сущности с позицией. Для раздельных добавления и снятия целевое хранилище подготавливается вне таймера. В цикле одна пара считается двумя операциями.
- Время сравнения берётся из `real_time`. Для наносекунд на элемент медиану делят на число совпадений, а для структурных операций на число операций. Исходные повторы и коэффициент вариации записываются в JSON. Подготовка мира, проверка результата и очистка фикстур находятся вне измеряемого участка.

`Ikigai/MixedRows/Lookup` использует тот же порядок ID из sparse-пула, что и `IkigaiMixed`. Таймер охватывает поиск записи и суммирование номеров архетипных строк. Время делится на число ID в sparse-пуле.

```powershell
$filter = '^(movement<IkigaiMixed>/100000/(10|50)/1/|movement<(IkigaiCompact|EnTT|FlecsUncached|FlecsCached)>/entities:100000/match_pct:(10|50)/signatures:1/|Ikigai/MixedRows/)'
& $exe "--benchmark_filter=$filter" --benchmark_min_time=0.08s --benchmark_min_warmup_time=0.02 --benchmark_repetitions=7 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/mixed-lookup.json --benchmark_out_format=json
```

Для диагностики Ikigai доступны `IkigaiNoHandle` с вызовом `addComponents`, `IkigaiWithListeners` с подписчиками на события и `Ikigai/AddTransition` с холодным и повторным переходом. В каждом замере `AddTransition` выполняется по одному добавлению в 64 отдельных мирах. Для времени на добавление значение из JSON делится на 64.

`Ikigai/EntityRegistry/GenerateIds` измеряет только выдачу ID. Для `RegisterKnownIds` заранее готовятся 100 000 последовательных ID, а таймер охватывает регистрацию в новом мире. `RegisterSparseIds` регистрирует 1 000 загруженных ID с шагом 65 537. В страничном реестре такой шаг давал отдельную страницу и отдельный каталог страниц на ID. Гибрид хранит эти записи компактно. Подготовка ID и проверка реестра в обоих сценариях выполняются вне таймера.

```powershell
& $exe '--benchmark_filter=^(structural<.*Structure::(Create|Destroy)>|Ikigai/EntityRegistry/)' --benchmark_min_time=0.04s --benchmark_min_warmup_time=0.01 --benchmark_repetitions=7 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/lifecycle.json --benchmark_out_format=json
```

На Windows отдельный исполняемый файл измеряет память пустых сущностей через снимки занятых блоков кучи CRT. Мир, пустой архетип и буфер ID готовятся перед первым снимком. Программа выводит прирост живых байтов после регистрации и остаток после уничтожения всех сущностей. Зарезервированная память процесса и свободные блоки в эти числа не входят.

```powershell
cmake --build build/ecs --config Release --target ecs_registry_memory
& './build/ecs/benchmarks/ecs/Release/ecs_registry_memory.exe'
```

Диагностика роста использует обе схемы хранения и миры с 1, 4 и 8 сохраняемыми колонками. Прогрев выполняется на временных сущностях. После него измеряемые сущности создаются в одном и том же порядке.

```powershell
& $exe '--benchmark_filter=^Ikigai/StorageGrowth/' --benchmark_min_time=0.04s --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/row-growth-comparison.json --benchmark_out_format=json
```

В `SharedRows` сценарий `TargetLookup` измеряет поиск записи сущности и выбор прогретого перехода. `MovePrepared` получает план заранее и измеряет поиск записи, добавление нового компонента, перенос сохранённых колонок и освобождение исходной строки. Ёмкость целевой таблицы подготовлена заранее. `Grow` и `Reuse` используют публичный вызов с возвратом handle. Замеры этапов запускаются отдельными проходами.

```powershell
$filter = '^(Ikigai/StorageGrowth/SharedRows/|Ikigai/AddTransition/|structural<(IkigaiCompact|IkigaiNoHandle|EnTT|FlecsUncached), Structure::(Add|Remove|AddRemove)>)'
& $exe "--benchmark_filter=$filter" --benchmark_min_time=0.04s --benchmark_min_warmup_time=0.01 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/archetype-transition.json --benchmark_out_format=json
```

`ecs_target_allocation` воспроизводит шаги роста хранилища пустого целевого архетипа на 100 000 строк. Для 1, 4 и 8 сохраняемых колонок в цели выделяются соответственно 2, 5 и 9 колонок. Таймер охватывает вызовы `reserve`. Для непрерывных поколений перед расширением заполняется живой диапазон, чтобы `reserve` переносил его как при реальном росте. Счётчик `copied_bytes` учитывает только этот диапазон. Бенчмарк сравнивает free-list и heap для чанков, прежний и новый шаг роста непрерывных поколений и экспериментальные сегменты поколений.

```powershell
cmake --build build/ecs --config Release --target ecs_target_allocation
& './build/ecs/benchmarks/ecs/Release/ecs_target_allocation.exe' --benchmark_min_time=0.03s --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/target-allocation.json --benchmark_out_format=json
```

## Пакетное добавление в существующие сущности

`ArchetypeBatch` добавляет одинаковый компонент к 1 000 или 100 000 существующих сущностей с 1, 4 или 8 сохраняемыми колонками. Цель прогревается одной временной сущностью. Резервирование и вставка входят в таймер, подготовка мира и проверка всех компонентов выполняются вне него.

`ArchetypeBatchSlice` выбирает средние 100 000 строк из 200 000 сущностей при 4 или 8 сохраняемых колонках. Проверка охватывает и перенесённые, и оставшиеся в источнике строки.

- Ikigai измеряет поштучные `addComponent` и `addComponents` и две версии `addArchetypeComponentBatch`. `ReservedRows` резервирует место заранее и переносит по строке. `Chunked` копирует тривиальные колонки блоками, если входные ID образуют непрерывный диапазон одного источника длиной не менее чанка. Хвост источника заполняет освободившиеся строки одним проходом. Остальные пакеты используют обычный путь.
- EnTT измеряет `emplace` и пару `storage.reserve` + `registry.insert` для диапазона. Хранилища остальных компонентов сохраняют свои строки.
- Flecs измеряет поштучный `entity.set`. В публичном `ecs_bulk_init` предоставленные ID должны принадлежать пустым сущностям, а в этой фикстуре у сущностей уже есть компоненты.

```powershell
& $exe '--benchmark_filter=^ArchetypeBatch(Slice)?/' --benchmark_min_time=0.03s --benchmark_min_warmup_time=0.01 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/archetype-batch.json --benchmark_out_format=json
```

`ArchetypeBatchScatter` выбирает 10% или 50% строк из 100 000 сущностей при 4 или 8 сохраняемых колонках. В `Strided` ID идут в порядке источника. `Shuffled` перемешивает те же ID с фиксированным seed. Таймер включает построение плана, резервирование, перенос и уплотнение. Итоговое время делится на число выбранных сущностей. Проверка охватывает весь исходный набор.

`Grouped` сравнивает новый путь Ikigai с `ReservedRows` из того же бинарника. Группировка применяется к пакетам тривиальных строк с четырьмя и более сохраняемыми колонками на источник. Путь требует уникальных ID и отсутствия добавляемого компонента. EnTT использует резервирование и вставку диапазона, Flecs — поштучный `entity.set` для существующих сущностей.

```powershell
& $exe '--benchmark_filter=^ArchetypeBatchScatter/' --benchmark_min_time=0.02s --benchmark_min_warmup_time=0.005 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/archetype-batch-scatter.json --benchmark_out_format=json
```

`ArchetypeScatterFocus` повторяет сценарий `Shuffled` при 10% выбранных сущностей и восьми сохраняемых колонках. `SourceChunks`, `SourceRowsBitmap` и `TargetChunksBitmap` измеряют одни и те же фазы с разным порядком переноса. В JSON `plan_ns` включает поиск записей, резервирование и построение плана, `target_ns` включает заполнение цели и обновление записей перемещённых сущностей, `compact_ns` включает уплотнение источника и обновление номеров оставшихся строк. Счётчики уже нормированы на выбранную сущность. `Adaptive` вызывает публичный API без таймеров фаз. Сравнение с EnTT и Flecs выполняется в том же прогоне.

```powershell
& $exe '--benchmark_filter=^ArchetypeScatterFocus/' --benchmark_min_time=0.02s --benchmark_min_warmup_time=0.005 --benchmark_repetitions=7 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/archetype-scatter-focus.json --benchmark_out_format=json
```

Для контрольного замера процесс закреплялся за логическим CPU 18 через `ProcessorAffinity` с маской `1 -shl 18`.
