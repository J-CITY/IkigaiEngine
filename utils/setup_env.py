import os
import shutil
import sys
import subprocess

_UTILS_DIR = os.path.dirname(os.path.abspath(__file__))
if _UTILS_DIR not in sys.path:
    sys.path.insert(0, _UTILS_DIR)

from sync_assets import ensure_assets

GLEW_VERSION = "2.3.1"
SPINE_VERSION = "4.2"
MAC_BREW_PACKAGES = (
    "glew",
    "vulkan-headers",
    "vulkan-loader",
    "molten-vk",
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


def is_environment_ready(root_dir=None, platform="win"):
    root_dir = repo_root(root_dir)
    platform = (platform or "win").lower()

    if not has_submodules(root_dir) or not has_spine(root_dir):
        return False
    if platform == "win" and not has_glew(root_dir):
        return False
    if platform == "mac" and missing_mac_brew_packages():
        return False
    return True


def main(platform="win"):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = repo_root()
    platform = (platform or "win").lower()

    print("Updating git submodules...")
    subprocess.run(["git", "submodule", "update", "--init", "--recursive"], cwd=root_dir, check=True)

    if platform == "win":
        print("\nUpdating GLEW (update_glew.py)...")
        subprocess.run([sys.executable, os.path.join(script_dir, "update_glew.py")], cwd=root_dir, check=True)

    if platform == "mac":
        print("\nChecking macOS Homebrew packages...")
        ensure_mac_brew_packages()

    if platform == "quest":
        print("\nFetching OpenXR Android loader (fetch_meta_openxr.py)...")
        subprocess.run([sys.executable, os.path.join(script_dir, "fetch_meta_openxr.py")], cwd=root_dir, check=True)

    print("\nUpdating Spine...")
    subprocess.run([sys.executable, os.path.join(script_dir, "update_spine.py")], cwd=root_dir, check=True)

    print("\nChecking game assets...")
    ensure_assets(root_dir)

    print("\nEnvironment setup complete!")


if __name__ == "__main__":
    requested = sys.argv[1] if len(sys.argv) > 1 else "win"
    main(requested)
