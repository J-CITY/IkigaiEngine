import json
import os
import re
from collections import defaultdict
from functools import cache
from pathlib import Path

from networkx import DiGraph, strongly_connected_components
from pyvis.network import Network

PROJECT_DIR = Path(__file__).resolve().parents[2]
ENGINE_DIR = PROJECT_DIR / "src" / "engine"
THIRD_PARTY_DIR = PROJECT_DIR / "3rd"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".m", ".mm"}
EDGE_COLORS = {
    "incoming": "#5ab0ff",
    "outgoing": "#47dc9a",
    "cycle": "#ff6b6b",
    "neutral": "#888888",
}

# Дополнительные каталоги из конфигураций платформ и библиотек.
EXTRA_INCLUDE_DIRS = {
    "IkigaiScript": ("SimpleScript",),
    "magic_enum": ("include/magic_enum",),
    "vfspp": ("vendor/miniz-cpp",),
}

# Комментарии и литералы пропускаем целиком, чтобы не считать директивы внутри них.
INCLUDE_DIRECTIVES = re.compile(
    r"""
    //[^\r\n]* | /\*[\s\S]*?\*/
    | R"(?P<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delimiter)"
    | ^[ \t]*\#[ \t]*include[ \t]*(?:"(?P<quoted>[^"\r\n]+)"|<(?P<angled>[^>\r\n]+)>)
    | "(?:\\.|[^"\\\r\n])*" | '(?:\\.|[^'\\\r\n])*'
    """,
    re.MULTILINE | re.VERBOSE,
)


def get_library_directories(third_party_dir):
    """Сопоставляет названия библиотек с их каталогами в 3rd."""
    libraries = {}
    if not third_party_dir.is_dir():
        return libraries

    for directory in sorted(third_party_dir.iterdir()):
        if not directory.is_dir() or directory.name.startswith("."):
            continue
        libraries[directory.name] = directory
        if directory.name in {"boost", "imgui"}:
            for submodule in sorted(directory.iterdir()):
                if submodule.is_dir() and not submodule.name.startswith("."):
                    libraries[f"{directory.name}/{submodule.name}"] = submodule
    return libraries


def get_file_owner(path, engine_dir, third_party_dir, libraries):
    if path.is_relative_to(engine_dir):
        parts = path.relative_to(engine_dir).parts
        return parts[0] if len(parts) > 1 else None
    if path.is_relative_to(third_party_dir):
        parts = path.relative_to(third_party_dir).parts
        if len(parts) > 1:
            submodule = "/".join(parts[:2])
            return submodule if submodule in libraries else parts[0]
    return None


def make_include_resolver(engine_dir, third_party_dir, libraries):
    include_dirs = []
    for directory in libraries.values():
        for prefix in ("", "include", "single_include", "src", *EXTRA_INCLUDE_DIRS.get(directory.name, ())):
            include_dir = directory / prefix
            if include_dir.is_dir():
                include_dirs.append(include_dir)
    emscripten_include_dir = third_party_dir / "emsdk/upstream/emscripten/cache/sysroot/include"

    @cache
    def find_library(include_path):
        # В sysroot также лежат заголовки стандартной библиотеки C.
        search_dirs = include_dirs
        if include_path.parts[:1] == ("emscripten",):
            search_dirs = [*include_dirs, emscripten_include_dir]
        owners = set()
        for directory in search_dirs:
            candidate = directory / include_path
            if candidate.is_file():
                owners.add(get_file_owner(candidate.resolve(), engine_dir, third_party_dir, libraries))
        owners.discard(None)
        # Разные библиотеки могут предоставлять одно и то же имя заголовка.
        return next(iter(owners)) if len(owners) == 1 else None

    def resolve(include_path, local_dir):
        path = Path(include_path.replace("\\", "/"))
        if local_dir is not None:
            candidate = local_dir / path
            if candidate.is_file():
                return get_file_owner(candidate.resolve(), engine_dir, third_party_dir, libraries)
        for directory in (engine_dir, third_party_dir.parent):
            candidate = directory / path
            if candidate.is_file():
                return get_file_owner(candidate.resolve(), engine_dir, third_party_dir, libraries)
        if path.is_absolute() or ".." in path.parts:
            return None
        return find_library(path)

    return resolve


def analyze_dependencies(engine_dir=ENGINE_DIR, third_party_dir=THIRD_PARTY_DIR):
    engine_dir = Path(engine_dir).resolve()
    third_party_dir = Path(third_party_dir).resolve()
    dependencies = defaultdict(lambda: defaultdict(int))
    libraries = get_library_directories(third_party_dir)
    third_party_libs = set(libraries)
    if not engine_dir.is_dir():
        print(f"❌ Ошибка: Директория {engine_dir} не найдена!")
        return dependencies, set(), third_party_libs

    internal_modules = {path.name for path in engine_dir.iterdir() if path.is_dir()}
    print(f"📦 Найдено внутренних модулей: {len(internal_modules)} ({', '.join(sorted(internal_modules))})")
    print(f"📚 Найдено 3rd-party библиотек/сабмодулей: {len(third_party_libs)}")
    resolve_include = make_include_resolver(engine_dir, third_party_dir, libraries)

    for root, _, files in os.walk(engine_dir):
        source_dir = Path(root)
        module_parts = source_dir.relative_to(engine_dir).parts
        if not module_parts:
            continue
        current_module = module_parts[0]

        for filename in files:
            source = source_dir / filename
            if source.suffix not in SOURCE_SUFFIXES:
                continue
            try:
                contents = source.read_text(encoding="utf-8-sig", errors="ignore")
            except OSError as error:
                print(f"Ошибка чтения файла {source}: {error}")
                continue
            for match in INCLUDE_DIRECTIVES.finditer(contents):
                quoted = match["quoted"]
                include_path = quoted or match["angled"]
                if include_path:
                    owner = resolve_include(include_path, source_dir if quoted else None)
                    if owner and owner != current_module:
                        dependencies[current_module][owner] += 1

    return dependencies, internal_modules, third_party_libs


def find_cycle_groups(dependencies, internal_modules):
    """Находит группы модулей, между которыми есть пути в обе стороны."""
    graph = DiGraph()
    graph.add_edges_from(
        (source, target)
        for source, targets in dependencies.items()
        if source in internal_modules
        for target in targets
        if target in internal_modules
    )
    return sorted(tuple(sorted(group)) for group in strongly_connected_components(graph) if len(group) > 1)


def print_cycle_report(cycle_groups):
    print(f"Циклические группы модулей ({len(cycle_groups)})")
    for number, modules in enumerate(cycle_groups, start=1):
        print(f"  {number}. {', '.join(modules)}")


def index_cycle_groups(cycle_groups):
    return {module: number for number, modules in enumerate(cycle_groups, start=1) for module in modules}


def edge_cycle_group(source, target, group_by_module):
    group = group_by_module.get(source)
    return group if group == group_by_module.get(target) else None


def print_report_table(dependencies, cycle_groups=()):
    """Выводит отчет в виде Markdown-совместимой ASCII-таблицы."""
    group_by_module = index_cycle_groups(cycle_groups)
    header = f"{'Исходный модуль движка':<25} | {'От чего зависит':<35} | {'Кол-во #include':>15} | Циклическая группа"
    border = "=" * len(header)
    print("\n" + border)
    print(header)
    print(border)

    for source in sorted(dependencies.keys()):
        targets = dependencies[source]
        # Сортируем по частоте использования (по убыванию)
        for target, count in sorted(targets.items(), key=lambda item: (-item[1], item[0])):
            group = edge_cycle_group(source, target, group_by_module)
            cycle = f"№{group}" if group is not None else "—"
            print(f"{source:<25} | {target:<35} | {count:>15} | {cycle}")
    print(border + "\n")

def write_dependency_graph(net):
    legend = f"""
    <div style="background:#1a1a1a;color:#ddd;padding:8px 16px">
        Нажмите на узел
        <span style="color:{EDGE_COLORS['incoming']};margin-left:1em">Входящие</span>
        <span style="color:{EDGE_COLORS['outgoing']};margin-left:1em">Исходящие</span>
        <span style="color:{EDGE_COLORS['cycle']};margin-left:1em">Циклы (пунктир)</span>
    </div>
    """
    interaction = """
    <script id="edge-directions">
    (() => {
        const colors = __EDGE_COLORS__;

        function highlightDirections(selectedNodes) {
            const selected = selectedNodes.length === 1 ? selectedNodes[0] : null;
            const updates = edges.map((edge) => {
                let color = colors.neutral;
                if (edge.dashes) {
                    color = colors.cycle;
                } else if (edge.to === selected) {
                    color = colors.incoming;
                } else if (edge.from === selected) {
                    color = colors.outgoing;
                }
                return {id: edge.id, color: {color, highlight: color, hover: color, inherit: false}};
            });
            edges.update(updates);
        }

        network.on("select", ({nodes}) => highlightDirections(nodes));
        highlightDirections([]);
    })();
    </script>
    """.replace("__EDGE_COLORS__", json.dumps(EDGE_COLORS))
    graph_container = '<div id="mynetwork"'
    html = net.generate_html().replace(graph_container, legend + graph_container, 1)
    body, footer = html.rsplit("</body>", 1)
    output = Path("engine_dependencies.html")
    output.write_text(body + interaction + "</body>" + footer, encoding="utf-8")
    print(f"🎉 Интерактивный граф сохранен в: {output}")


def generate_graph(dependencies, internal_modules, cycle_groups):
    """Генерирует интерактивный HTML-граф зависимостей движка."""
    net = Network(height="850px", width="100%", directed=True, bgcolor="#1a1a1a", font_color="white", cdn_resources="in_line")
    group_by_module = index_cycle_groups(cycle_groups)
    
    # Настройка физики (кулоновское отталкивание и пружины для чистоты графа)
    net.force_atlas_2based(gravity=-50, central_gravity=0.01, spring_length=120)
    
    # Собираем только те узлы, которые реально участвуют в связях (чтобы не спамить пустые либы)
    active_nodes = set(dependencies.keys())
    for targets in dependencies.values():
        active_nodes.update(targets.keys())
        
    for node in sorted(active_nodes):
        if node in internal_modules:
            color = "#2ecc71"  # Ярко-зеленый для модулей движка
            group = "Internal"
            title = f"Модуль движка: {node}"
            size = 30
        else:
            color = "#e67e22"  # Оранжевый для 3rd party
            group = "3rdParty"
            title = f"Сторонняя библиотека: {node}"
            size = 20
            
        net.add_node(node, label=node, color=color, title=title, size=size, group=group)
        if node in group_by_module:
            net.get_node(node).update(
                color={"background": color, "border": EDGE_COLORS["cycle"]},
                borderWidth=3,
                title=f"{title}<br>Циклическая группа {group_by_module[node]}",
            )
        
    # Добавляем ребра-стрелки
    for source, targets in sorted(dependencies.items()):
        for target, count in sorted(targets.items()):
            cycle_group = edge_cycle_group(source, target, group_by_module)
            cyclic = cycle_group is not None
            title = f"Зависимостей (#include): {count}"
            if cyclic:
                title += f"<br>Циклическая группа {cycle_group}"
            # Толщина стрелки масштабируется в зависимости от количества включений
            width = min(1 + (count // 5), 8) 
            net.add_edge(
                source, 
                target, 
                value=count, 
                width=width,
                title=title,
                color=EDGE_COLORS["cycle" if cyclic else "neutral"],
                dashes=cyclic,
                arrowStrikethrough=False
            )
            
    write_dependency_graph(net)

if __name__ == "__main__":
    print("🔍 Автоматическое сканирование архитектуры проекта...")
    deps, internals, _ = analyze_dependencies()
    
    if deps:
        cycle_groups = find_cycle_groups(deps, internals)
        print_report_table(deps, cycle_groups)
        print_cycle_report(cycle_groups)
        generate_graph(deps, internals, cycle_groups)
    else:
        print("⚠ Связей между модулями не обнаружено. Проверьте, доступны ли каталоги src/engine и 3rd.")
