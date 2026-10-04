import os
import sys
import subprocess
import argparse
from datetime import datetime

ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
UTILS_DIR = os.path.join(ROOT_DIR, "utils")
if UTILS_DIR not in sys.path:
    sys.path.insert(0, UTILS_DIR)

from setup_env import is_environment_ready, main as setup_environment
from sync_assets import ensure_assets
from ensure_emsdk import ensure_emsdk, emrun_path

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
    path = arg if arg else 'out'
    if not os.path.isabs(path):
        path = os.path.join(platform_dir, path)
    return os.path.abspath(path)


def create_assets_link(build_dir):
    import platform

    assets_src = os.path.abspath('./assets')
    assets_dest = os.path.abspath(os.path.join(build_dir, 'assets'))

    if not os.path.exists(assets_src):
        return

    if os.path.exists(assets_dest):
        return

    try:
        if platform.system() == 'Windows':
            subprocess.run(['cmd', '/c', 'mklink', '/J', assets_dest, assets_src], stdout=subprocess.DEVNULL)
        else:
            os.symlink(assets_src, assets_dest, target_is_directory=True)
        print(f"Created assets link in {build_dir}")
    except Exception as e:
        print(f"Warning: Failed to create assets link: {e}")


def ensure_environment(platform_arg, skip_setup):
    if skip_setup:
        return
    if is_environment_ready(ROOT_DIR, platform_arg):
        ensure_assets(ROOT_DIR)
        return
    print("Environment is not ready. Running utils/setup_env.py...")
    setup_environment(platform_arg)


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


def run_cmake_build(build_dir, build_type, env=None):
    run_logged(['cmake', '--build', build_dir, '--config', build_type], env=env, check=True)


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


def resolve_engine_features(platform, use_editor, use_file_watcher):
    use_editor = True if use_editor is None else bool(use_editor)
    if use_file_watcher is None:
        if platform in ('web', 'android'):
            use_file_watcher = False
        else:
            use_file_watcher = use_editor
    if not use_editor:
        use_file_watcher = False
    return use_editor, use_file_watcher


def append_engine_feature_cmake_args(command, use_editor, use_file_watcher):
    command.append('-DUSE_EDITOR=' + ('ON' if use_editor else 'OFF'))
    command.append('-DUSE_FILE_WATCHER=' + ('ON' if use_file_watcher else 'OFF'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', type=str, default="vs22", help='Compiller type: vs22 (default: vs22)')
    parser.add_argument('-a', type=str, default="x64", help='Architecture: x86, x64 (default: x64)')
    parser.add_argument('-p', type=str, default="win", help='Platform: win, linux, macos, android, quest, ios, web (default: win)')
    parser.add_argument(
        '--use-editor',
        dest='use_editor',
        nargs='?',
        const=True,
        default=True,
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
    parser.add_argument('-g', type=str, default="opengl", choices=['opengl', 'vulkan', 'dx12'], help='Graphics API (default: opengl)')
    parser.add_argument('--esVer', dest='es_ver', type=str, default='3.2', choices=['3', '3.1', '3.2'],
                        help='Android OpenGL ES version (default: 3.2)')
    parser.add_argument('--abi', type=str, default='arm64-v8a,armeabi-v7a',
                        help='Comma-separated Android/Quest ABIs (android default: arm64-v8a,armeabi-v7a; quest default: arm64-v8a)')
    parser.add_argument('-b', type=str, default=None, help='CMake generate directory (default: <platform>/build)')
    parser.add_argument('-o', type=str, default=None, help='Runtime output directory for the executable (default: <platform>/out)')
    parser.add_argument('-s', type=str, default='windows', choices=['windows', 'console'],
                        help='Windows subsystem: windows (no console) or console (default: windows)')
    parser.add_argument('-t', '--build-type', type=str, default='Release', choices=['Debug', 'Release'],
                        help='CMake build type: Debug or Release (default: Release)')
    parser.add_argument('--build', action='store_true', help='Build the project after CMake configure')
    parser.add_argument('--run', action='store_true', help='Run the web build with emrun from 3rd/emsdk')
    parser.add_argument('-S', '--skip-setup', action='store_true', help='Skip automatic environment setup')
    parser.add_argument('--log', type=str, default='run_vs.log',
                        help='Log file path (default: run_vs.log in the repo root)')
    args = parser.parse_args()

    log_path = args.log
    if not os.path.isabs(log_path):
        log_path = os.path.join(ROOT_DIR, log_path)

    abort_code = 0
    try:
        abs_log = open_log(log_path)
        print(f"Logging to {abs_log}")
        print(f"Started: {datetime.now().isoformat(timespec='seconds')}")
        print(f"Args: {' '.join(sys.argv)}")
        print(f"Cwd: {os.getcwd()}")

        platform_arg = args.p.lower()
        build_type = args.build_type
        use_editor, use_file_watcher = resolve_engine_features(
            platform_arg, args.use_editor, args.use_file_watcher
        )
        if platform_arg == 'quest' and (args.use_editor is False or args.use_file_watcher is not None):
            print('Note: Quest build has no editor or file watcher; --use-editor / --use_file_watcher are ignored.')
        ensure_environment(platform_arg, args.skip_setup)

        if platform_arg == 'win':
            build_dir = args.b if args.b else './windows/build'
            output_dir = resolve_runtime_output(args.o, './windows')
            command = [
                'cmake', './windows', f'-B{build_dir}',
                f'-DENGINE_RUNTIME_OUTPUT_DIRECTORY={output_dir}',
                f'-DCMAKE_BUILD_TYPE={build_type}',
            ]
            if (args.c == 'vs22'):
                command.extend(['-G', 'Visual Studio 17 2022', '-T', 'host=x64'])
            elif (args.c == 'vs19'):
                command.extend(['-G', 'Visual Studio 16 2019', '-T', 'host=x64'])

            if (args.a == 'x64'):
                command.extend(['-A', 'x64'])

            command.append('-DUSE_OPENGL=' + ('ON' if args.g == 'opengl' else 'OFF'))
            command.append('-DUSE_VULKAN=' + ('ON' if args.g == 'vulkan' else 'OFF'))
            command.append('-DUSE_DX12=' + ('ON' if args.g == 'dx12' else 'OFF'))
            command.append(f'-DENGINE_WIN_SUBSYSTEM={args.s}')
            append_engine_feature_cmake_args(command, use_editor, use_file_watcher)

            run_logged(command, check=True)
            create_assets_link(build_dir)
            create_assets_link(output_dir)
            if args.build:
                run_cmake_build(build_dir, build_type)

        elif platform_arg == 'mac':
            build_dir = args.b if args.b else './mac/build'
            output_dir = resolve_runtime_output(args.o, './mac')
            command = [
                'cmake', './mac', f'-B{build_dir}',
                f'-DENGINE_RUNTIME_OUTPUT_DIRECTORY={output_dir}',
                f'-DCMAKE_BUILD_TYPE={build_type}',
            ]
            command.append('-DUSE_OPENGL=' + ('ON' if args.g == 'opengl' else 'OFF'))
            command.append('-DUSE_VULKAN=' + ('ON' if args.g == 'vulkan' else 'OFF'))
            append_engine_feature_cmake_args(command, use_editor, use_file_watcher)
            run_logged(command, check=True)
            create_assets_link(build_dir)
            create_assets_link(output_dir)
            if args.build:
                run_cmake_build(build_dir, build_type)

        elif platform_arg == 'web':
            emsdk_env = ensure_emsdk(ROOT_DIR)
            build_dir = args.b if args.b else './emscripten/build'
            output_dir = resolve_runtime_output(args.o, './emscripten')
            if args.build or not args.run:
                command = [
                    'emcmake', 'cmake', './emscripten', f'-B{build_dir}',
                    '-G', 'Ninja',
                    f'-DENGINE_RUNTIME_OUTPUT_DIRECTORY={output_dir}',
                    f'-DCMAKE_BUILD_TYPE={build_type}',
                ]
                append_engine_feature_cmake_args(command, use_editor, use_file_watcher)
                run_logged(command, env=emsdk_env, check=True, shell=(os.name == 'nt'))
                create_assets_link(build_dir)
                create_assets_link(output_dir)
                if args.build:
                    run_cmake_build(build_dir, build_type, env=emsdk_env)
            if args.run:
                html_path = os.path.join(output_dir, 'IkigaiEngine.html')
                if not os.path.isfile(html_path):
                    raise FileNotFoundError(f"Web build not found: {html_path}. Run with --build first.")
                run_logged([emrun_path(ROOT_DIR), html_path], env=emsdk_env, check=True)

        elif platform_arg == 'android':
            from generate_android import generate_android

            output_dir = args.b if args.b else os.path.join(ROOT_DIR, 'android', 'out')
            if not os.path.isabs(output_dir):
                output_dir = os.path.abspath(os.path.join(ROOT_DIR, output_dir))
            abis = [part.strip() for part in args.abi.split(',') if part.strip()]
            generate_android(
                ROOT_DIR,
                output_dir,
                es_ver=args.es_ver,
                abis=abis,
                use_editor=use_editor,
                use_file_watcher=use_file_watcher,
            )
            if args.build:
                gradlew_name = 'gradlew.bat' if os.name == 'nt' else 'gradlew'
                gradlew = os.path.join(output_dir, gradlew_name)
                run_logged([gradlew, ':app:assembleDebug'], cwd=output_dir, check=True)

        elif platform_arg == 'quest':
            from generate_quest import generate_quest

            output_dir = args.b if args.b else os.path.join(ROOT_DIR, 'quest', 'out')
            if not os.path.isabs(output_dir):
                output_dir = os.path.abspath(os.path.join(ROOT_DIR, output_dir))
            default_abi = 'arm64-v8a,armeabi-v7a'
            abi_arg = args.abi if args.abi != default_abi else 'arm64-v8a'
            abis = [part.strip() for part in abi_arg.split(',') if part.strip()]
            generate_quest(ROOT_DIR, output_dir, abis=abis)
            if args.build:
                gradlew_name = 'gradlew.bat' if os.name == 'nt' else 'gradlew'
                gradlew = os.path.join(output_dir, gradlew_name)
                run_logged([gradlew, ':app:assembleDebug'], cwd=output_dir, check=True)

        else:
            print(f"Unknown platform: {platform_arg}")
            abort_code = 1

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
    main()
