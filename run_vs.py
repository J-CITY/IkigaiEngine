import os
import sys
import subprocess
import argparse
import json
import re
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
from utils.setup_env import ensure_mac_brew_packages, ensure_render_sdks, is_environment_ready, main as setup_environment
from utils.assets.sync_assets import ensure_assets
from utils.environment.ensure_emsdk import ensure_emsdk, emrun_path
from utils.environment.render_sdk_options import add_render_sdk_arguments, RenderSdkOptions
from utils.build.cmake_presets import load_configure_preset

LOG_FILE = None


class Tee:
    def __init__(self, *streams):
        self.streams = streams

    def write(self, data):
        for stream in self.streams:
            stream.write(data)
            stream.flush()

    def flush(self):
        for stream in self.streams:
            stream.flush()

    def isatty(self):
        return False


def log_line(text=""):
    print(text)
    if LOG_FILE is not None:
        LOG_FILE.flush()


def open_log(path):
    global LOG_FILE
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    LOG_FILE = open(path, "w", encoding="utf-8", errors="replace")
    sys.stdout = Tee(sys.__stdout__, LOG_FILE)
    sys.stderr = Tee(sys.__stderr__, LOG_FILE)
    return os.path.abspath(path)


def close_log():
    global LOG_FILE
    sys.stdout = sys.__stdout__
    sys.stderr = sys.__stderr__
    if LOG_FILE is not None:
        LOG_FILE.close()
        LOG_FILE = None


def resolve_runtime_output(arg, platform_dir):
    path = arg if arg else os.path.join('out', platform_dir)
    if not os.path.isabs(path):
        path = os.path.join(ROOT_DIR, path)
    return os.path.abspath(path)


def create_content_links(directory):
    assets = Path(ROOT_DIR) / 'assets'
    destination = Path(directory) / 'assets'
    try:
        if not assets.is_dir() or (destination.exists() and not destination.samefile(assets)):
            return
        Path(directory).mkdir(parents=True, exist_ok=True)
        # The runtime mounts packs beside assets. Link packs first to avoid
        # exposing a partially prepared content root on a new build.
        for name in ('assets_packs', 'assets'):
            source = Path(ROOT_DIR) / name
            link = Path(directory) / name
            if not source.is_dir() or link.exists():
                continue
            if os.name == 'nt':
                subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(source)],
                               stdout=subprocess.DEVNULL, check=True)
            else:
                link.symlink_to(source, target_is_directory=True)
            print(f"Created {name} link in {directory}")
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Warning: Failed to prepare content links in {directory}: {error}")


def ensure_environment(platform_arg, skip_setup, render_sdks, graphics_backends, generator, use_lua, use_ikigai_script):
    if skip_setup:
        return
    scripting_deps = []
    if use_lua:
        scripting_deps.extend((('3rd/lua', 'lua.h'), ('3rd/sol', 'include/sol/sol.hpp')))
    if use_ikigai_script:
        scripting_deps.append(('3rd/IkigaiScript', 'SimpleScript/Library/ikigaiScript.h'))
    missing = [path for path, header in scripting_deps if not os.path.isfile(os.path.join(ROOT_DIR, path, header))]
    if missing:
        subprocess.run(['git', 'submodule', 'update', '--init', '--recursive', '--', *missing], cwd=ROOT_DIR, check=True)
    if platform_arg == "mac":
        ensure_mac_brew_packages()
    if is_environment_ready(ROOT_DIR, platform_arg):
        ensure_assets(ROOT_DIR)
        ensure_render_sdks(render_sdks, graphics_backends, generator=generator)
        return
    print("Environment is not ready. Running utils/setup_env.py...")
    setup_environment(platform_arg, render_sdks=render_sdks, graphics_backends=graphics_backends,
                      generator=generator, use_lua=use_lua, use_ikigai_script=use_ikigai_script)


def ensure_windows_openssl(architecture, values, environment):
    if (cmake_bool(values.get('CMAKE_TOOLCHAIN_FILE', '')) or environment.get('CMAKE_TOOLCHAIN_FILE')
            or 'OPENSSL_ROOT_DIR' in environment or cmake_bool(values.get('OpenSSL_DIR', ''))):
        return

    target = architecture.lower()
    triplet = f'{target}-windows-static-md'
    vcpkg = Path(ROOT_DIR) / 'utils' / '.pack' / 'vcpkg'
    package_dir = vcpkg / 'installed' / triplet
    configured_dir = values.get('OPENSSL_ROOT_DIR', '')
    if cmake_bool(configured_dir) and Path(configured_dir).resolve() != package_dir.resolve():
        return
    if target not in ('x86', 'x64', 'arm64'):
        raise ValueError(f'Automatic OpenSSL setup does not support {architecture}. Set OPENSSL_ROOT_DIR for that target.')

    def package_ready():
        return (package_dir / 'include/openssl/ssl.h').is_file() and all(
            (package_dir / folder / library).is_file()
            for folder in ('lib', 'debug/lib') for library in ('libssl.lib', 'libcrypto.lib')
        )

    if package_ready():
        return
    print(f'Preparing OpenSSL for {triplet} with vcpkg...')
    bootstrap = vcpkg / 'bootstrap-vcpkg.bat'
    if not bootstrap.is_file():
        vcpkg.parent.mkdir(parents=True, exist_ok=True)
        run_logged(['git', 'clone', '--depth', '1', '--branch', '2026.10.16',
                    'https://github.com/microsoft/vcpkg.git', str(vcpkg)], cwd=ROOT_DIR, env=environment)
    executable = vcpkg / 'vcpkg.exe'
    if not executable.is_file():
        run_logged(['cmd.exe', '/d', '/c', bootstrap.name, '-disableMetrics'], cwd=vcpkg, env=environment)
    run_logged([str(executable), 'install', f'openssl:{triplet}', '--classic'],
               cwd=vcpkg, env=environment)
    if not package_ready():
        raise RuntimeError(f'vcpkg did not provide both Debug and Release OpenSSL libraries in {package_dir}')


def format_command(command):
    if isinstance(command, (list, tuple)):
        return " ".join(str(part) for part in command)
    return str(command)


def run_logged(command, env=None, shell=False, check=True, cwd=None):
    print(f"\n$ {format_command(command)}")
    process = subprocess.Popen(
        command,
        env=env,
        shell=shell,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    assert process.stdout is not None
    for line in process.stdout:
        sys.stdout.write(line)
    returncode = process.wait()
    if check and returncode != 0:
        raise subprocess.CalledProcessError(returncode, command)
    return returncode


def _optional_bool(value):
    if value is True or value is False:
        return value
    if isinstance(value, str):
        lowered = value.lower()
        if lowered in ('1', 'true', 'yes', 'on', 't', 'y'):
            return True
        if lowered in ('0', 'false', 'no', 'off', 'f', 'n'):
            return False
    raise argparse.ArgumentTypeError(f'expected a boolean value, got {value!r}')


def normalize_platform(name):
    name = (name or '').lower()
    return {'windows': 'win', 'macos': 'mac', 'osx': 'mac', 'darwin': 'mac',
            'emscripten': 'web'}.get(name, name)


def cmake_value(value):
    if isinstance(value, bool):
        return 'ON' if value else 'OFF'
    return str(value)


def cmake_bool(value):
    return str(value).upper() not in ('', '0', 'OFF', 'NO', 'FALSE', 'N', 'IGNORE', 'NOTFOUND') and not str(value).upper().endswith('-NOTFOUND')


def read_cmake_cache(build_dir):
    path = os.path.join(build_dir, 'CMakeCache.txt')
    if not os.path.isfile(path):
        return {}
    values = {}
    with open(path, encoding='utf-8') as stream:
        for line in stream:
            if line.startswith(('#', '//')) or '=' not in line or ':' not in line.split('=', 1)[0]:
                continue
            key, value = line.rstrip('\n').split('=', 1)
            values[key.split(':', 1)[0]] = value
    return values


@contextmanager
def setup_environment_variables(environment):
    previous = dict(os.environ)
    try:
        os.environ.clear()
        os.environ.update(environment)
        yield
    finally:
        os.environ.clear()
        os.environ.update(previous)


def resolve_options(platform, values, environment):
    # CMake owns defaults and capability validation, including pre-setup queries.
    command = ['cmake', '-DIKIGAI_PLATFORM=' + platform]
    command += [f'-D{key}={cmake_value(value)}' for key, value in values.items()
                if key.startswith('USE_') or key in ('IKIGAI_BUILD_APPLICATION', 'IKIGAI_NETWORK_TLS')]
    command += ['-P', os.path.join(ROOT_DIR, 'cmake', 'ResolveBuildOptions.cmake')]
    result = subprocess.run(command, cwd=ROOT_DIR, env=environment, capture_output=True, text=True)
    if result.returncode:
        raise ValueError((result.stdout + result.stderr).strip())
    for line in (result.stdout + result.stderr).splitlines():
        if line.startswith('IKIGAI_OPTIONS_JSON='):
            return json.loads(line.split('=', 1)[1])
    raise RuntimeError('CMake did not return resolved build options')


def select_preset(args):
    if args.preset:
        return args.preset
    platform = normalize_platform(args.p or 'win')
    if platform == 'win':
        if args.c not in (None, 'vs22', 'vs19'):
            raise ValueError('Windows generator must be vs22 or vs19')
        return 'windows-vs2019' if args.c == 'vs19' else 'windows'
    if platform == 'mac':
        if args.c not in (None, 'vs22', 'vs19', 'xcode'):
            raise ValueError('macOS generator must be xcode, or omitted for Unix Makefiles')
        return 'mac-xcode' if args.c == 'xcode' else 'mac-' + (args.build_type or 'Release').lower()
    if platform not in ('linux', 'ios', 'web', 'android', 'quest'):
        raise ValueError(f'Unknown platform: {platform}')
    if platform in ('linux', 'web'):
        return platform + '-' + (args.build_type or 'Release').lower()
    return platform


def configure_and_build(args):
    preset_name = select_preset(args)
    parent_environment = dict(os.environ)
    preset = load_configure_preset(ROOT_DIR, preset_name, parent_environment)
    environment = preset['_environment']
    available = subprocess.run(['cmake', '--list-presets'], cwd=ROOT_DIR, env=parent_environment,
                               capture_output=True, text=True, check=True)
    if preset_name not in re.findall(r'^\s*"([^"]+)"', available.stdout, re.MULTILINE):
        raise ValueError(f'Preset {preset_name!r} is not available on this host/environment')
    metadata = preset.get('vendor', {}).get('ikigai.dev/launcher/1.0', {})
    platform = normalize_platform(metadata.get('platform') or preset['_cache'].get('IKIGAI_PLATFORM', ''))
    if not platform or platform == 'auto':
        raise ValueError('Launcher presets must specify platform metadata or IKIGAI_PLATFORM')
    if args.p and normalize_platform(args.p) != platform:
        raise ValueError('-p conflicts with the selected preset platform')
    if args.run and platform != 'web':
        raise ValueError('--run is supported only for Web builds')

    overrides = {}
    for argument, key in ((args.use_lua, 'USE_LUA'), (args.use_ikigai_script, 'USE_IKIGAI_SCRIPT'),
                          (args.use_editor, 'USE_EDITOR'), (args.use_file_watcher, 'USE_FILE_WATCHER')):
        if argument is not None:
            overrides[key] = cmake_value(argument)
    if args.g is not None:
        for backend in ('opengl', 'vulkan', 'dx12', 'metal'):
            overrides['USE_' + backend.upper()] = cmake_value(backend in args.g)
    if args.s is not None:
        overrides['ENGINE_WIN_SUBSYSTEM'] = args.s
    for flag, key in ((args.dlss_sr, 'USE_DLSS'), (args.fsr_sr, 'USE_FSR'),
                      (args.dlss_fg, 'USE_DLSS_FRAME_GENERATION'), (args.fsr_fg, 'USE_FSR_FRAME_GENERATION')):
        if flag is not None:
            overrides[key] = cmake_value(flag)
    if args.dlss_fg:
        overrides['USE_STREAMLINE'] = 'ON'
    for definition in args.define:
        if '=' not in definition:
            raise ValueError('-D expects NAME=VALUE')
        key, value = definition.split('=', 1)
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*(?::[A-Za-z]+)?', key):
            raise ValueError(f'Invalid CMake definition: {key}')
        overrides[key.split(':', 1)[0]] = value
    if 'IKIGAI_PLATFORM' in overrides and normalize_platform(overrides['IKIGAI_PLATFORM']) != platform:
        raise ValueError('Choose a matching preset instead of overriding IKIGAI_PLATFORM')

    mobile = platform in ('android', 'quest')
    platform_dir = {'win': 'windows'}.get(platform, platform)
    if args.b:
        build_dir = os.path.abspath(os.path.join(ROOT_DIR, args.b))
    elif mobile:
        build_dir = os.path.join(ROOT_DIR, 'build', platform_dir)
    else:
        build_dir = preset['binaryDir']
    cache = {} if mobile else read_cmake_cache(build_dir)
    if cache.get('CMAKE_HOME_DIRECTORY') and os.path.normcase(os.path.abspath(cache['CMAKE_HOME_DIRECTORY'])) != os.path.normcase(ROOT_DIR):
        raise ValueError(f'{build_dir} contains an older platform CMake cache. Use a new -b directory (for example build/{preset_name}); existing files were not changed.')
    values = {**cache, **preset['_cache'], **{key.split(':', 1)[0]: value for key, value in overrides.items()}}
    build_type = args.build_type or values.get('CMAKE_BUILD_TYPE') or 'Release'
    if build_type not in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'):
        raise ValueError(f'Unsupported build configuration: {build_type}')
    if args.build_type is not None or not args.preset:
        overrides['CMAKE_BUILD_TYPE'] = build_type
    application = cmake_bool(values.get('IKIGAI_BUILD_APPLICATION', 'ON'))
    resolved = resolve_options(platform, values, environment)
    graphics = {key[4:].lower() for key in ('USE_OPENGL', 'USE_VULKAN', 'USE_DX12', 'USE_METAL') if resolved[key]}
    sdk = RenderSdkOptions(*(resolved[key] for key in
                           ('USE_DLSS', 'USE_FSR', 'USE_DLSS_FRAME_GENERATION', 'USE_FSR_FRAME_GENERATION')),
                           streamline_enabled=resolved['USE_STREAMLINE'])
    architecture = args.a or preset.get('architecture', 'x64')
    if isinstance(architecture, dict):
        architecture = architecture.get('value', 'x64')
    architecture = 'x86' if architecture == 'Win32' else architecture
    generator = preset.get('generator', '')
    generator_override = None
    if args.preset and args.c:
        if platform == 'win' and args.c in ('vs22', 'vs19'):
            generator_override = load_configure_preset(ROOT_DIR, 'windows' if args.c == 'vs22' else 'windows-vs2019', parent_environment)['generator']
        elif platform == 'mac' and args.c in ('xcode', 'vs22', 'vs19'):
            generator_override = load_configure_preset(ROOT_DIR, 'mac-xcode' if args.c == 'xcode' else 'mac', parent_environment)['generator']
        else:
            raise ValueError('-c conflicts with the selected preset')
    generator = generator_override or generator
    if args.a is not None and platform == 'win' and not generator.startswith('Visual Studio '):
        raise ValueError('-a requires a Visual Studio preset; use a toolchain for other generators')
    print(f'Configure preset: {preset_name}; platform: {platform}; configuration: {build_type}')
    print('Effective CMake features: ' + ', '.join(f'{key}={cmake_value(value)}' for key, value in resolved.items() if key.startswith('USE_')))
    if application:
        sdk.validate(platform, graphics, architecture)
        if (platform == 'win' and not args.skip_setup and not preset.get('toolchainFile')
                and resolved['USE_NETWORK'] and resolved['IKIGAI_NETWORK_TLS']):
            ensure_windows_openssl(architecture, values, environment)
        with setup_environment_variables(environment):
            ensure_environment(platform, args.skip_setup, sdk, graphics, generator,
                               resolved['USE_LUA'], resolved['USE_IKIGAI_SCRIPT'])
        if platform == 'web':
            with setup_environment_variables(parent_environment):
                parent_environment = ensure_emsdk(ROOT_DIR)
            environment = load_configure_preset(ROOT_DIR, preset_name, parent_environment)['_environment']
    if application:
        overrides.setdefault('Python3_EXECUTABLE', values.get('Python3_EXECUTABLE', sys.executable))
    if (sdk.fsr_sr or sdk.fsr_fg) and 'vulkan' in graphics and 'IKIGAI_FSR_VULKAN_RUNTIME' not in values:
        from utils.build.fidelityfx_vulkan import runtime_path
        overrides['IKIGAI_FSR_VULKAN_RUNTIME'] = runtime_path()

    if mobile:
        if not application:
            raise ValueError('Android/Quest Gradle packaging requires IKIGAI_BUILD_APPLICATION=ON')
        if build_type not in ('Debug', 'Release'):
            raise ValueError('Gradle packaging supports only Debug and Release')
        abi_value = args.abi or overrides.get('ANDROID_ABI') or (values.get('ANDROID_ABI') if args.preset else None)
        abis = [part.strip() for part in re.split('[,;]', abi_value) if part.strip()] if abi_value else None
        mobile_options = {**preset['_cache'], **overrides}
        artifact_output_dir = resolve_runtime_output(args.o, platform_dir)
        with setup_environment_variables(environment):
            if platform == 'android':
                from utils.build.generate_android import generate_android, ES_VERSIONS
                android_arguments = {}
                if args.es_ver:
                    android_arguments['es_ver'] = args.es_ver
                elif 'IKIGAI_GLES_VERSION' in values:
                    version = next((name for name, spec in ES_VERSIONS.items()
                                    if str(spec['cmake']) == str(values['IKIGAI_GLES_VERSION'])), None)
                    if version is None:
                        raise ValueError('Invalid IKIGAI_GLES_VERSION in selected preset')
                    android_arguments['es_ver'] = version
                generate_android(ROOT_DIR, build_dir, abis=abis,
                                 cmake_cache_variables=mobile_options, artifact_output_dir=artifact_output_dir, **android_arguments)
            else:
                from utils.build.generate_quest import generate_quest
                generate_quest(ROOT_DIR, build_dir, abis=abis, cmake_cache_variables=mobile_options,
                               artifact_output_dir=artifact_output_dir)
        if args.build:
            wrapper = os.path.join(build_dir, 'gradlew.bat' if os.name == 'nt' else 'gradlew')
            run_logged([wrapper, ':app:assemble' + build_type], cwd=build_dir, env=environment)
        return

    if args.o:
        overrides['ENGINE_RUNTIME_OUTPUT_DIRECTORY'] = resolve_runtime_output(args.o, platform_dir)
    command = ['cmake', '--preset', preset_name, '-S', ROOT_DIR, '-B', build_dir]
    if generator_override:
        command += ['-G', generator_override]
    if args.a is not None and platform == 'win':
        command += ['-A', 'Win32' if args.a == 'x86' else args.a]
    command += [f'-D{key}={value}' for key, value in overrides.items()]
    if args.build or not args.run:
        run_logged(command, cwd=ROOT_DIR, env=parent_environment)
    actual_cache = read_cmake_cache(build_dir)
    output_dir = (overrides.get('ENGINE_RUNTIME_OUTPUT_DIRECTORY') if args.run and not args.build else None)
    output_dir = output_dir or actual_cache.get('ENGINE_RUNTIME_OUTPUT_DIRECTORY') or values.get('ENGINE_RUNTIME_OUTPUT_DIRECTORY')
    if output_dir:
        output_dir = os.path.abspath(os.path.join(ROOT_DIR, output_dir, build_type))
    if application and output_dir:
        create_content_links(build_dir)
        create_content_links(output_dir)
    if args.build:
        command = ['cmake', '--build', build_dir, '--config', build_type]
        if args.target:
            command += ['--target', *args.target]
        if args.parallel:
            command += ['--parallel', str(args.parallel)]
        run_logged(command, cwd=ROOT_DIR, env=environment)
    if generator == 'Xcode':
        print('Xcode project: ' + os.path.join(build_dir, 'IkigaiEngine.xcodeproj'))
    if args.run:
        html_path = os.path.join(output_dir or build_dir, 'IkigaiEngine.html')
        if not os.path.isfile(html_path):
            raise FileNotFoundError(f'Web build not found: {html_path}. Run with --build first.')
        run_logged([emrun_path(ROOT_DIR), html_path], cwd=ROOT_DIR, env=environment)


def main():
    parser = argparse.ArgumentParser(description='Configure an engine preset; legacy arguments override its settings.')
    parser.add_argument('--preset', help='Configure preset from CMakePresets.json or CMakeUserPresets.json')
    parser.add_argument('--list-presets', action='store_true', help='List available CMake configure presets')
    parser.add_argument('-D', dest='define', action='append', default=[], metavar='NAME=VALUE', help='Override a CMake cache variable')
    parser.add_argument('--target', nargs='+', help='CMake build targets (default: all)')
    parser.add_argument('-j', '--parallel', type=int, help='Parallel build jobs')
    parser.add_argument("--use-lua", type=_optional_bool, nargs="?", const=True, default=None)
    parser.add_argument("--use-ikigai-script", type=_optional_bool, nargs="?", const=True, default=None)
    parser.add_argument(
        '-c', type=str, default=None,
        help='Generator: vs22, vs19 (Windows); xcode (macOS, default on mac is Unix Makefiles)',
    )
    parser.add_argument('-a', type=str, choices=['x86', 'x64'], default=None, help='Architecture: x86, x64 (default: x64)')
    parser.add_argument(
        '-p', type=str, default=None,
        help='Platform: win, mac/macos, linux, android, quest, web, ios (default: win)',
    )
    parser.add_argument(
        '--use-editor',
        dest='use_editor',
        nargs='?',
        const=True,
        default=None,
        type=_optional_bool,
        metavar='BOOL',
        help='Editor UI (default: on). --use-editor or --use-editor 1; --use-editor 0 / false to disable.',
    )
    parser.add_argument(
        '--use_file_watcher',
        dest='use_file_watcher',
        nargs='?',
        const=True,
        default=None,
        type=_optional_bool,
        metavar='BOOL',
        help='Asset file watching / hot reload (default: on for win/mac when editor on; off for web/android).',
    )
    parser.add_argument(
        '-g',
        nargs='+',
        default=None,
        choices=['opengl', 'vulkan', 'dx12', 'metal'],
        metavar='API',
        help=(
            'Graphics APIs compiled into one binary (default: opengl). '
            'Several values share one build, e.g. -g opengl vulkan metal. '
            'mac: opengl, vulkan, metal. win: opengl, vulkan, dx12. '
            'linux: opengl, vulkan. '
            'Pick the active API at launch via Configs/render.json or --render-backend=. '
            'A binary with one API uses it if the asset config requests an unavailable API.'
        ),
    )
    parser.add_argument('--esVer', dest='es_ver', type=str, default=None, choices=['3', '3.1', '3.2'],
                        help='Android OpenGL ES version (default: 3.2)')
    parser.add_argument('--abi', type=str, default=None,
                        help='Comma-separated Android/Quest ABIs (android default: arm64-v8a,armeabi-v7a; quest default: arm64-v8a)')
    parser.add_argument('-b', type=str, default=None, help='Build directory (default: build/<platform>, with preset variants in subdirectories)')
    parser.add_argument('-o', type=str, default=None, help='Output base directory (default: out/<platform>; configuration is appended)')
    parser.add_argument('-s', type=str, default=None, choices=['windows', 'console'],
                        help='Windows subsystem: windows (no console) or console (default: windows)')
    parser.add_argument('-t', '--build-type', type=str, default=None, choices=['Debug', 'Release'],
                        help='CMake build type: Debug or Release (default: Release)')
    parser.add_argument('--build', action='store_true', help='Build the project after CMake configure')
    add_render_sdk_arguments(parser)
    parser.set_defaults(dlss_sr=None, fsr_sr=None, dlss_fg=None, fsr_fg=None)
    parser.add_argument('--run', action='store_true', help='Run the web build with emrun from 3rd/emsdk')
    parser.add_argument('-S', '--skip-setup', action='store_true', help='Skip automatic environment setup')
    logging = parser.add_mutually_exclusive_group()
    logging.add_argument('--log', default='run_vs.log',
                         help='Log file path (default: run_vs.log in the repo root)')
    logging.add_argument('--no-log', dest='log', action='store_const', const=None,
                         help='Print output to the terminal without writing a log file')
    args = parser.parse_args()
    if args.list_presets:
        raise SystemExit(subprocess.call(['cmake', '--list-presets'], cwd=ROOT_DIR))
    if args.parallel is not None and args.parallel < 1:
        parser.error('--parallel must be positive')

    log_path = args.log
    if log_path is not None and not os.path.isabs(log_path):
        log_path = os.path.join(ROOT_DIR, log_path)

    abort_code = 0
    try:
        if log_path is not None:
            print(f"Logging to {open_log(log_path)}")
        print(f"Started: {datetime.now().isoformat(timespec='seconds')}")
        print(f"Args: {' '.join(sys.argv)}")
        print(f"Cwd: {os.getcwd()}")

        configure_and_build(args)

        print(f"Finished: {datetime.now().isoformat(timespec='seconds')}")
    except subprocess.CalledProcessError as exc:
        print(f"Command failed with exit code {exc.returncode}: {format_command(exc.cmd)}")
        abort_code = exc.returncode or 1
    except Exception as exc:
        print(f"run_vs.py failed: {exc}")
        abort_code = 1
    finally:
        if LOG_FILE is not None:
            print(f"Log saved to {os.path.abspath(log_path)}")
            close_log()

    if abort_code:
        sys.exit(abort_code)


if __name__ == '__main__':
    from utils.environment.python_env import ensure_project_python
    ensure_project_python()
    main()
