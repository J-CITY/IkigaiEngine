import os
import shutil
import sys
import subprocess

if __package__ in (None, ''):
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils.assets.sync_assets import ensure_assets
from utils.environment.render_sdk_options import RenderSdkOptions, add_render_sdk_arguments, render_sdk_options
from utils.environment.render_sdks import ensure_render_sdks, render_sdks_ready

GLEW_VERSION = "2.3.1"
SPINE_VERSION = "4.2"
MAC_BREW_PACKAGES = (
    "glew",
    "vulkan-headers",
    "vulkan-loader",
    "molten-vk",
)

LINUX_PACMAN_PACKAGES = (
    "cmake",
    "base-devel",
    "mesa",
    "glew",
    "vulkan-headers",
    "vulkan-validation-layers",
    "libx11",
    "libxext",
    "wayland",
    "libxkbcommon",
    "libpulse",
    "alsa-lib",
)

LINUX_APT_PACKAGES = (
    "cmake",
    "build-essential",
    "libgl1-mesa-dev",
    "libglew-dev",
    "libvulkan-dev",
    "vulkan-validationlayers",
    "libx11-dev",
    "libxext-dev",
    "libwayland-dev",
    "libxkbcommon-dev",
    "libpulse-dev",
    "libasound2-dev",
)


def repo_root(root_dir=None):
    if root_dir:
        return os.path.abspath(root_dir)
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def _version_matches(path, expected):
    if not os.path.isfile(path):
        return False
    with open(path, "r", encoding="utf-8") as f:
        return f.read().strip() == expected


def has_submodules(root_dir):
    return os.path.isfile(os.path.join(root_dir, "3rd", "assimp", "CMakeLists.txt"))


def has_glew(root_dir):
    return _version_matches(os.path.join(root_dir, "3rd", "glew", "version.txt"), GLEW_VERSION)


def has_spine(root_dir):
    return _version_matches(os.path.join(root_dir, "3rd", "spine", "spine", "version.txt"), SPINE_VERSION)


def brew_package_installed(name):
    brew = shutil.which("brew")
    if not brew:
        return False
    result = subprocess.run(
        [brew, "list", "--formula", name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def missing_mac_brew_packages():
    return [name for name in MAC_BREW_PACKAGES if not brew_package_installed(name)]


def ensure_mac_brew_packages():
    missing = missing_mac_brew_packages()
    if not missing:
        print("macOS Homebrew packages are already installed.")
        return
    brew = shutil.which("brew")
    if not brew:
        raise RuntimeError(
            "Homebrew is required for macOS dependencies (glew, vulkan-loader, molten-vk). "
            "Install it from https://brew.sh and re-run."
        )
    print("Installing macOS packages: " + " ".join(missing))
    subprocess.run([brew, "install", *missing], check=True)


def apt_package_installed(name):
    dpkg = shutil.which("dpkg")
    if not dpkg:
        return False
    result = subprocess.run(
        [dpkg, "-s", name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def missing_linux_apt_packages():
    return [name for name in LINUX_APT_PACKAGES if not apt_package_installed(name)]


def pacman_package_installed(name):
    pacman = shutil.which("pacman")
    if not pacman:
        return False
    result = subprocess.run(
        [pacman, "-Qq", name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def missing_linux_pacman_packages():
    return [name for name in LINUX_PACMAN_PACKAGES if not pacman_package_installed(name)]


def ensure_linux_packages():
    if shutil.which("apt-get"):
        missing = missing_linux_apt_packages()
        if not missing:
            print("Linux apt packages are already installed.")
            return
        print("Please install the missing Linux packages. You may be prompted for your password:")
        print(f"sudo apt-get install {' '.join(missing)}")
        try:
            subprocess.run(["sudo", "apt-get", "install", "-y", *missing], check=True)
        except Exception as e:
            print(f"Failed to install packages: {e}")
            print("Please install them manually.")
    elif shutil.which("pacman"):
        missing = missing_linux_pacman_packages()
        if not missing:
            print("Linux pacman packages are already installed.")
            return
        print("Please install the missing Linux packages. You may be prompted for your password:")
        print(f"sudo pacman -S {' '.join(missing)}")
        try:
            subprocess.run(["sudo", "pacman", "-S", "--noconfirm", *missing], check=True)
        except Exception as e:
            print(f"Failed to install packages: {e}")
            print("Please install them manually.")
    else:
        print("Warning: Neither 'apt-get' nor 'pacman' found. Please ensure dependencies are installed.")


def update_base_submodules(root_dir, use_lua=True, use_ikigai_script=True):
    from utils.environment.sdks import SDK_NAMES
    vendor_paths = {sdk[0] for sdk in SDK_NAMES.values()}
    if not use_lua:
        vendor_paths.update(('3rd/lua', '3rd/sol'))
    if not use_ikigai_script:
        vendor_paths.add('3rd/IkigaiScript')
    paths = subprocess.run(['git', 'config', '-f', '.gitmodules', '--get-regexp',
                            r'^submodule\..*\.path$'], cwd=root_dir, capture_output=True, text=True)
    if paths.returncode not in (0, 1):
        raise subprocess.CalledProcessError(paths.returncode, paths.args, paths.stdout, paths.stderr)
    selected = [line.split(None, 1)[1] for line in paths.stdout.splitlines()
                if line.split(None, 1)[1] not in vendor_paths]
    if selected:
        subprocess.run(['git', 'submodule', 'update', '--init', '--recursive', '--', *selected],
                       cwd=root_dir, check=True)


def is_environment_ready(root_dir=None, platform="win", render_sdks=None, graphics_backends=()):
    root_dir = repo_root(root_dir)
    platform = (platform or "win").lower()
    if render_sdks is not None:
        try:
            render_sdks.validate(platform, graphics_backends)
        except ValueError:
            return False

    if not has_submodules(root_dir) or not has_spine(root_dir):
        return False
    if platform == "win" and not has_glew(root_dir):
        return False
    if platform == "mac" and missing_mac_brew_packages():
        return False
    if platform == "linux":
        if shutil.which("apt-get") and missing_linux_apt_packages():
            return False
        elif shutil.which("pacman") and missing_linux_pacman_packages():
            return False
    return render_sdks_ready(render_sdks, graphics_backends)


def main(platform="win", render_sdks=None, graphics_backends=(), generator='Visual Studio 17 2022', use_lua=True, use_ikigai_script=True):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = repo_root()
    platform = (platform or "win").lower()
    render_sdks = render_sdks or RenderSdkOptions()
    render_sdks.validate(platform, graphics_backends)

    print("Updating git submodules...")
    update_base_submodules(root_dir, use_lua, use_ikigai_script)

    if platform == "win":
        print("\nUpdating GLEW (update_glew.py)...")
        subprocess.run([sys.executable, os.path.join(script_dir, "environment", "update_glew.py")], cwd=root_dir, check=True)

    if platform == "mac":
        print("\nChecking macOS Homebrew packages...")
        ensure_mac_brew_packages()

    if platform == "linux":
        print("\nChecking Linux packages...")
        ensure_linux_packages()

    if platform == "quest":
        print("\nFetching OpenXR Android loader (fetch_meta_openxr.py)...")
        subprocess.run([sys.executable, os.path.join(script_dir, "environment", "fetch_meta_openxr.py")], cwd=root_dir, check=True)

    print("\nUpdating Spine...")
    subprocess.run([sys.executable, os.path.join(script_dir, "environment", "update_spine.py")], cwd=root_dir, check=True)

    ensure_render_sdks(render_sdks, graphics_backends, generator)

    print("\nChecking game assets...")
    ensure_assets(root_dir)

    print("\nEnvironment setup complete!")


if __name__ == "__main__":
    import argparse
    from utils.environment.python_env import ensure_project_python
    ensure_project_python()
    parser = argparse.ArgumentParser(description='Prepare engine dependencies and selected Windows SR/FG SDKs.')
    parser.add_argument('platform', nargs='?', default='win', choices=('win', 'mac', 'linux', 'ios', 'web', 'android', 'quest'))
    parser.add_argument('-g', nargs='+', default=['opengl'], choices=('opengl', 'vulkan', 'dx12', 'metal'))
    parser.add_argument('--generator', default='Visual Studio 17 2022', help='Generator for the FSR Vulkan SDK source build')
    add_render_sdk_arguments(parser)
    args = parser.parse_args()
    main(args.platform, render_sdk_options(args), args.g, args.generator)
