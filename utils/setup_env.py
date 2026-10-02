import os
import sys
import subprocess

GLEW_VERSION = "2.3.1"
SPINE_VERSION = "4.2"


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


def is_environment_ready(root_dir=None, platform="win"):
    root_dir = repo_root(root_dir)
    platform = (platform or "win").lower()

    if not has_submodules(root_dir) or not has_spine(root_dir):
        return False
    if platform == "win" and not has_glew(root_dir):
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

    print("\nUpdating Spine...")
    subprocess.run([sys.executable, os.path.join(script_dir, "update_spine.py")], cwd=root_dir, check=True)

    print("\nEnvironment setup complete!")


if __name__ == "__main__":
    requested = sys.argv[1] if len(sys.argv) > 1 else "win"
    main(requested)
