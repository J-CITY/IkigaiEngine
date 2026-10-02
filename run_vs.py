import os
import subprocess
import argparse


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', type=str, default="vs22", help='Compiller type: vs22 (default: vs22)')
    parser.add_argument('-a', type=str, default="x64", help='Architecture: x86, x64 (default: x64)')
    parser.add_argument('-p', type=str, default="win", help='Platform: win, uwp, linux , macos, android, ios, web, switch (default: win)')
    parser.add_argument('-e', type=bool, default=True, help='Edittor mode (default: True)')
    parser.add_argument('-g', type=str, default="opengl", choices=['opengl', 'vulkan', 'dx12'], help='Graphics API (default: opengl)')
    parser.add_argument('-b', type=str, default=None, help='CMake generate directory (default: <platform>/build)')
    parser.add_argument('-o', type=str, default=None, help='Runtime output directory for the executable (default: <platform>/out)')
    args = parser.parse_args()

    platform_arg = args.p.lower()

    if platform_arg == 'win':
        build_dir = args.b if args.b else './windows/build'
        output_dir = resolve_runtime_output(args.o, './windows')
        command = ['cmake', './windows', f'-B{build_dir}', f'-DENGINE_RUNTIME_OUTPUT_DIRECTORY={output_dir}']
        if (args.c == 'vs22'):
            command.extend(['-G', 'Visual Studio 17 2022', '-T', 'host=x64'])
        elif (args.c == 'vs19'):
            command.extend(['-G', 'Visual Studio 16 2019', '-T', 'host=x64'])
        
        if (args.a == 'x64'):
            command.extend(['-A', 'x64'])
            
        command.append('-DUSE_OPENGL=' + ('ON' if args.g == 'opengl' else 'OFF'))
        command.append('-DUSE_VULKAN=' + ('ON' if args.g == 'vulkan' else 'OFF'))
        command.append('-DUSE_DX12=' + ('ON' if args.g == 'dx12' else 'OFF'))
        
        subprocess.run(command)
        create_assets_link(build_dir)
        create_assets_link(output_dir)

    elif platform_arg == 'mac':
        build_dir = args.b if args.b else './mac/build'
        output_dir = resolve_runtime_output(args.o, './mac')
        command = ['cmake', './mac', f'-B{build_dir}', f'-DENGINE_RUNTIME_OUTPUT_DIRECTORY={output_dir}']
        command.append('-DUSE_OPENGL=' + ('ON' if args.g == 'opengl' else 'OFF'))
        command.append('-DUSE_VULKAN=' + ('ON' if args.g == 'vulkan' else 'OFF'))
        subprocess.run(command)
        create_assets_link(build_dir)
        create_assets_link(output_dir)

    elif platform_arg == 'web':
        build_dir = args.b if args.b else './emscripten/build'
        output_dir = resolve_runtime_output(args.o, './emscripten')
        command = ['emcmake', 'cmake', './emscripten', f'-B{build_dir}', f'-DENGINE_RUNTIME_OUTPUT_DIRECTORY={output_dir}']
        subprocess.run(command, shell=(os.name == 'nt'))
        create_assets_link(build_dir)
        create_assets_link(output_dir)

    elif platform_arg == 'android':
        build_dir = args.b if args.b else './android/build'
        command = ['cmake', './android', f'-B{build_dir}']
        subprocess.run(command)
        create_assets_link(build_dir)

    elif platform_arg == 'oculus':
        build_dir = args.b if args.b else './oculus/build'
        command = ['cmake', './oculus', f'-B{build_dir}']
        subprocess.run(command)
        create_assets_link(build_dir)
    
    else:
        print(f"Unknown platform: {platform_arg}")


if __name__ == '__main__':
    main()