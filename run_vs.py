import subprocess
import argparse


def create_assets_link(build_dir):
    import os
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
    args = parser.parse_args()

    platform_arg = args.p.lower()

    if platform_arg == 'win':
        command = ['cmake', './windows', '-B./windows/build']
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
        create_assets_link('./windows/build')

    elif platform_arg == 'mac':
        command = ['cmake', './mac', '-B./mac/build']
        command.append('-DUSE_OPENGL=' + ('ON' if args.g == 'opengl' else 'OFF'))
        command.append('-DUSE_VULKAN=' + ('ON' if args.g == 'vulkan' else 'OFF'))
        subprocess.run(command)
        create_assets_link('./mac/build')

    elif platform_arg == 'web':
        import os
        command = ['emcmake', 'cmake', './emscripten', '-B./emscripten/build']
        subprocess.run(command, shell=(os.name == 'nt'))
        create_assets_link('./emscripten/build')

    elif platform_arg == 'android':
        command = ['cmake', './android', '-B./android/build']
        subprocess.run(command)
        create_assets_link('./android/build')

    elif platform_arg == 'oculus':
        command = ['cmake', './oculus', '-B./oculus/build']
        subprocess.run(command)
        create_assets_link('./oculus/build')
    
    else:
        print(f"Unknown platform: {platform_arg}")


if __name__ == '__main__':
    main()