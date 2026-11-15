"""Read configure-preset inputs for setup; CMake still validates and executes presets.

This helper does not evaluate conditions or duplicate engine feature defaults.
The launcher checks availability with CMake before using these setup inputs.
"""

import copy
import json
import os
from pathlib import Path
import platform
import re


_MAP_FIELDS = {'cacheVariables', 'environment', 'vendor', 'warnings', 'errors', 'debug'}
_LOCAL_FIELDS = {'name', 'hidden', 'inherits', 'description', 'displayName'}
_MACRO = re.compile(r'\$(?P<namespace>[A-Za-z]*)\{(?P<name>[^{}]*)\}')


def load_configure_preset(root_dir, name, environment=None):
    """Return expanded fields plus ``_cache`` strings and full ``_environment``.

    Relative binary directories are resolved against the repository root.
    Unsupported macros fail explicitly instead of guessing setup/toolchain paths.
    """
    root = Path(root_dir).resolve()
    # Windows exposes environment names case-insensitively, including $penv{Path}.
    def env_key(key):
        return key.upper() if os.name == 'nt' else key

    parent_env = {env_key(key): value for key, value in
                  (os.environ if environment is None else environment).items()}
    records = {}
    file_presets = {}
    includes = {}
    visiting_files = set()

    def read_file(path, implicit_include=None):
        path = path.resolve()
        if path in visiting_files:
            raise ValueError(f'Cyclic CMake preset include: {path}')
        if path in file_presets:
            return
        visiting_files.add(path)
        try:
            with path.open(encoding='utf-8-sig') as stream:
                document = json.load(stream)
            children = []
            if implicit_include is not None:
                children.append(implicit_include)
            for filename in document.get('include', []):
                # CMake 3.27+ permits parent-environment macros in include paths.
                def include_macro(match):
                    if match['namespace'] != 'penv':
                        raise ValueError(f'Unsupported preset include macro: {match[0]}')
                    return parent_env.get(env_key(match['name']), '')
                children.append(path.parent / _MACRO.sub(include_macro, filename))
            includes[path] = {child.resolve() for child in children}
            for child in children:
                read_file(child)
            entries = document.get('configurePresets', [])
            file_presets[path] = set()
            for preset in entries:
                preset_name = preset['name']
                if preset_name in records:
                    raise ValueError(f'Duplicate configure preset: {preset_name}')
                records[preset_name] = (preset, path)
                file_presets[path].add(preset_name)
        finally:
            visiting_files.remove(path)

    project_file = root / 'CMakePresets.json'
    read_file(project_file)
    user_file = root / 'CMakeUserPresets.json'
    if user_file.exists():
        read_file(user_file, project_file)

    def visible_files(path):
        result = {path}
        for child in includes[path]:
            result.update(visible_files(child))
        return result

    def merge(base, override):
        result = copy.deepcopy(base)
        for key, value in override.items():
            if key in _MAP_FIELDS:
                result[key] = {**result.get(key, {}), **copy.deepcopy(value)}
            else:
                result[key] = copy.deepcopy(value)
        return result

    def inherit(preset_name, active=()):
        if preset_name in active:
            raise ValueError(f'Cyclic CMake preset inheritance: {" -> ".join((*active, preset_name))}')
        if preset_name not in records:
            raise ValueError(f'Unknown configure preset: {preset_name}')
        preset, owner = records[preset_name]
        parents = preset.get('inherits', [])
        if isinstance(parents, str):
            parents = [parents]
        result = {}
        # Earlier parents take precedence, including null map entries.
        for parent in reversed(parents):
            if parent not in records or records[parent][1] not in visible_files(owner):
                raise ValueError(f'Preset {preset_name} cannot inherit unavailable preset {parent}')
            inherited = inherit(parent, (*active, preset_name))
            result = merge(result, {k: v for k, v in inherited.items() if k not in _LOCAL_FIELDS})
        return merge(result, preset)

    preset = inherit(name)
    if preset.get('hidden', False):
        raise ValueError(f'Cannot select hidden configure preset: {name}')
    macros = {
        'sourceDir': root.as_posix(),
        'sourceParentDir': root.parent.as_posix(),
        'sourceDirName': root.name,
        'presetName': name,
        'generator': preset.get('generator', parent_env.get('CMAKE_GENERATOR', '')),
        'hostSystemName': platform.system(),
        'dollar': '$',
        'pathListSep': os.pathsep,
    }
    # Preset-to-preset variable references use exact JSON keys; only the inherited
    # process environment and the final Windows process map ignore name case.
    overrides = preset.get('environment', {})
    resolved_env = {}
    active_env = set()

    def env_value(key):
        if key in resolved_env:
            return resolved_env[key]
        if key in active_env:
            raise ValueError(f'Cyclic environment variable in preset {name}: {key}')
        value = overrides.get(key)
        if value is None:
            return parent_env.get(env_key(key), '')
        active_env.add(key)
        try:
            resolved_env[key] = expand(value)
            return resolved_env[key]
        finally:
            active_env.remove(key)

    def expand(value):
        if not isinstance(value, str):
            return value
        def substitute(match):
            namespace, key = match['namespace'], match['name']
            if namespace == 'env':
                return env_value(key)
            if namespace == 'penv':
                return parent_env.get(env_key(key), '')
            if not namespace and key in macros:
                return macros[key]
            raise ValueError(f'Unsupported macro in preset {name}: {match[0]}')
        return _MACRO.sub(substitute, value)

    effective_env = dict(parent_env)
    for key, value in sorted(overrides.items()):
        if value is not None:
            effective_env[env_key(key)] = env_value(key)
    cache = {}
    for key, value in preset.get('cacheVariables', {}).items():
        if value is None:
            continue
        if isinstance(value, dict):
            value = value['value']
        cache[key] = ('ON' if value else 'OFF') if isinstance(value, bool) else expand(str(value))

    for field in ('binaryDir', 'installDir', 'toolchainFile'):
        if field in preset:
            preset[field] = expand(preset[field])
    if 'binaryDir' in preset:
        preset['binaryDir'] = str((root / preset['binaryDir']).resolve())
    preset['_cache'] = cache
    preset['_environment'] = effective_env
    return preset
