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

Для диагностики Ikigai доступны `IkigaiNoHandle` с вызовом `addComponents`, `IkigaiWithListeners` с подписчиками на события и `Ikigai/AddTransition` с холодным и повторным переходом. В каждом замере `AddTransition` выполняется по одному добавлению в 64 отдельных мирах. Для времени на добавление значение из JSON делится на 64.

`Ikigai/EntityRegistry/GenerateIds` измеряет только выдачу ID. Для `RegisterKnownIds` ID готовятся заранее, а таймер охватывает регистрацию в новом мире.

```powershell
& $exe '--benchmark_filter=^(structural<.*Structure::(Create|Destroy)>|Ikigai/EntityRegistry/)' --benchmark_min_time=0.04s --benchmark_min_warmup_time=0.01 --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/lifecycle.json --benchmark_out_format=json
```

Диагностика роста использует обе схемы хранения и миры с 1, 4 и 8 сохраняемыми колонками. Прогрев выполняется на временных сущностях. После него измеряемые сущности создаются в одном и том же порядке.

```powershell
& $exe '--benchmark_filter=^Ikigai/StorageGrowth/' --benchmark_min_time=0.04s --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true --benchmark_out=build/ecs/row-growth-comparison.json --benchmark_out_format=json
```
