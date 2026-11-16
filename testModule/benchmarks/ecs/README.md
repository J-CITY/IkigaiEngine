# ECS benchmarks

[Результаты замеров](RESULTS.md)

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
& $exe --benchmark_dry_run
& $exe --benchmark_min_time=0.06s --benchmark_min_warmup_time=0.02 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/results.json --benchmark_out_format=json
```

| Репозиторий | SHA |
|---|---|
| EnTT | `d4014c74dc3793aba95ae354d6e23a026c2796db` |
| Flecs | `a53b4715c0b91e366bbfce53d9abd2b61534f7a3` |
| Google Benchmark | `eddb0241389718a23a42db6af5f0164b6e0139af` |

## Как читать результаты

- `movement` измеряет проход по сущностям. У всех ECS одинаковые позиция, скорость, шаг и доля совпадений. При 16 наборах компонентов четыре дополнительных тега задают все 16 комбинаций. EnTT использует `view`. Flecs измеряется с `QueryCacheNone` и `QueryCacheAuto`. `cachedQueryBuild` измеряет создание кэшированного запроса на уже заполненном мире с 16 наборами компонентов.
- `IkigaiMixed` хранит скорость в sparse-пуле. `IkigaiAligned` повторяет плотный проход с 16-байтными компонентами вместо 8-байтных.
- `structural` измеряет отдельно создание и удаление пустых сущностей и добавление со снятием компонента у сущности с позицией. Число операций в последнем сценарии равно удвоенному числу сущностей. `CommandBuffer` измеряет запись или применение команды добавления компонента.
- `Demo` запускает реальные системы из `src/ecsDemo` через `SystemManager::runUpdate`. В сценарии преследования у половины сущностей есть одна цель, у остальных целей нет. `Demo/Movement` и `Demo/Follower` измеряются отдельно.
- Время сравнения берётся из `real_time`. Для наносекунд на элемент медиану делят на число совпадений, а для структурных операций на число операций. Исходные повторы и коэффициент вариации записываются в JSON. Подготовка мира, проверка результата и очистка фикстур находятся вне измеряемого участка.

Для сравнения версий демо сохраняйте Release-бинарник каждой стадии до следующей правки. Запускайте каждый бинарник с одинаковыми параметрами и отдельным файлом JSON.

```powershell
& $exe '--benchmark_filter=^Demo/' --benchmark_min_time=0.12s --benchmark_min_warmup_time=0.03 --benchmark_repetitions=7 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/demo.json --benchmark_out_format=json
```
