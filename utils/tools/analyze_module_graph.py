import os
import re
from collections import defaultdict
from pyvis.network import Network

# ==================== НАСТРОЙКИ СТРУКТУРЫ ====================
ENGINE_DIR = "../../src/engine"
THIRD_PARTY_DIR = "../../3rd"
# =============================================================

def get_project_structure(engine_dir, third_party_dir):
    """Автоматически находит все внутренние модули и 3rd-party библиотеки."""
    internal_modules = set()
    third_party_libs = set()

    # 1. Сканируем внутренние модули (все папки первого уровня в src/engine)
    if os.path.exists(engine_dir):
        for entry in os.scandir(engine_dir):
            if entry.is_dir():
                internal_modules.add(entry.name)

    # 2. Сканируем 3rd-party (первый уровень + поддержка вложенности для imgui и boost)
    if os.path.exists(third_party_dir):
        for entry in os.scandir(third_party_dir):
            if entry.is_dir():
                # Если это imgui или boost, заходим на один уровень глубже для поиска сабмодулей
                if entry.name.lower() in ['imgui', 'boost']:
                    third_party_libs.add(entry.name) # Добавляем и саму корневую либу на всякий случай
                    for sub_entry in os.scandir(entry.path):
                        if sub_entry.is_dir():
                            # Записываем как "imgui/submodule" или "boost/submodule"
                            third_party_libs.add(f"{entry.name}/{sub_entry.name}")
                else:
                    third_party_libs.add(entry.name)

    return internal_modules, third_party_libs

def find_module_owner(include_path, internal_modules, third_party_libs):
    """Определяет, к какому модулю или 3rd party относится include-путь."""
    # Приводим к единому виду разделители путей
    normalized_path = include_path.replace('\\', '/')
    parts = normalized_path.split('/')
    
    if not parts:
        return None, None

    # Проверка на составные 3rd party (например, imgui/submodule/... или boost/submodule/...)
    if len(parts) >= 2:
        two_parts = f"{parts[0]}/{parts[1]}"
        if two_parts in third_party_libs:
            return two_parts, "3rdParty"

    # Проверка по первому элементу пути или по полному совпадению имени
    first_part = parts[0]
    if first_part in internal_modules:
        return first_part, "Internal"
    if first_part in third_party_libs:
        return first_part, "3rdParty"

    # Эвристика для плоских инклудов (если имя файла или путь содержит название либы)
    for lib in third_party_libs:
        if '/' in lib: # Для вложенных проверяем точное совпадение сабмодуля
            sub = lib.split('/')[-1]
            if sub.lower() in normalized_path.lower():
                return lib, "3rdParty"
        elif lib.lower() in normalized_path.lower():
            return lib, "3rdParty"
            
    return None, None

def analyze_dependencies():
    internal_modules, third_party_libs = get_project_structure(ENGINE_DIR, THIRD_PARTY_DIR)
    
    print(f"📦 Найдено внутренних модулей: {len(internal_modules)} ({', '.join(sorted(internal_modules))})")
    print(f"📚 Найдено 3rd-party библиотек/сабмодулей: {len(third_party_libs)}")
    
    dependencies = defaultdict(lambda: defaultdict(int))
    include_regex = re.compile(r'#include\s+["<](.*?)[">]')
    
    if not os.path.exists(ENGINE_DIR):
        print(f"❌ Ошибка: Директория {ENGINE_DIR} не найдена!")
        return dependencies, internal_modules, third_party_libs

    # Обходим всю папку src/engine
    for root, dirs, files in os.walk(ENGINE_DIR):
        # Вычисляем, к какому корневому модулю относится текущий файл
        relative_path = os.path.relpath(root, ENGINE_DIR)
        if relative_path == '.':
            continue
            
        current_module = relative_path.split(os.sep)[0]
        
        for file in files:
            if file.endswith(('.h', '.hpp', '.cpp', '.inl', '.cc', '.cxx')):
                file_path = os.path.join(root, file)
                try:
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        for line in f:
                            match = include_regex.search(line)
                            if match:
                                include_path = match.group(1)
                                target, target_type = find_module_owner(include_path, internal_modules, third_party_libs)
                                
                                # Исключаем self-зависимости модуля на самого себя
                                if target and target != current_module:
                                    dependencies[current_module][target] += 1
                except Exception as e:
                    print(f"Ошибка чтения файла {file_path}: {e}")
                    
    return dependencies, internal_modules, third_party_libs

def print_report_table(dependencies):
    """Выводит отчет в виде Markdown-совместимой ASCII-таблицы."""
    print("\n" + "="*85)
    print(f"{'Исходный модуль движка':<25} | {'От чего зависит':<35} | {'Кол-во #include'}")
    print("="*85)
    
    for source in sorted(dependencies.keys()):
        targets = dependencies[source]
        # Сортируем по частоте использования (по убыванию)
        for target, count in sorted(targets.items(), key=lambda x: x[1], reverse=True):
            print(f"{source:<25} | {target:<35} | {count}")
    print("="*85 + "\n")

def generate_graph(dependencies, internal_modules, third_party_libs):
    """Генерирует интерактивный HTML-граф зависимостей движка."""
    net = Network(height="850px", width="100%", directed=True, bgcolor="#1a1a1a", font_color="white")
    
    # Настройка физики (кулоновское отталкивание и пружины для чистоты графа)
    net.force_atlas_2based(gravity=-50, central_gravity=0.01, spring_length=120)
    
    # Собираем только те узлы, которые реально участвуют в связях (чтобы не спамить пустые либы)
    active_nodes = set(dependencies.keys())
    for targets in dependencies.values():
        active_nodes.update(targets.keys())
        
    for node in active_nodes:
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
        
    # Добавляем ребра-стрелки
    for source, targets in dependencies.items():
        for target, count in targets.items():
            # Толщина стрелки масштабируется в зависимости от количества включений
            width = min(1 + (count // 5), 8) 
            net.add_edge(
                source, 
                target, 
                value=count, 
                width=width,
                title=f"Зависимостей (#include): {count}", 
                color="#888888",
                arrowStrikethrough=False
            )
            
    output_filename = "engine_dependencies.html"
    net.write_html(output_filename)
    print(f"🎉 Интерактивный граф сохранен в: {output_filename}")

if __name__ == "__main__":
    print("🔍 Автоматическое сканирование архитектуры проекта...")
    deps, internals, third_party = analyze_dependencies()
    
    if deps:
        print_report_table(deps)
        generate_graph(deps, internals, third_party)
    else:
        print("⚠ Связей между модулями не обнаружено. Проверьте правильность путей SRC_DIR / ENGINE_DIR.")
