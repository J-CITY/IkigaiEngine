import os
import sys
import subprocess
import shutil


def _repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def read_emsdk_version(root_dir=None):
    root_dir = root_dir or _repo_root()
    version_file = os.path.join(root_dir, "emscripten", "emsdk.version")
    with open(version_file, "r", encoding="utf-8") as f:
        return f.read().strip()


def emsdk_dir(root_dir=None):
    root_dir = root_dir or _repo_root()
    return os.path.join(root_dir, "3rd", "emsdk")


def _emscripten_tool(sdk, name):
    emscripten_dir = os.path.join(sdk, "upstream", "emscripten")
    suffixes = [".exe", ".bat", ""] if os.name == "nt" else [""]
    for suffix in suffixes:
        candidate = os.path.join(emscripten_dir, name + suffix)
        if os.path.isfile(candidate):
            return candidate
    return None


def _emcc_path(sdk):
    return _emscripten_tool(sdk, "emcc")


def emrun_path(root_dir=None):
    sdk = emsdk_dir(root_dir)
    path = _emscripten_tool(sdk, "emrun")
    if not path:
        raise RuntimeError(
            "emrun was not found. Install the Emscripten SDK first "
            "(python run_vs.py -p web) or use: "
            f"{os.path.join(sdk, 'upstream', 'emscripten', 'emrun.exe')}"
        )
    return path


def _toolchain_paths(sdk):
    paths = [
        sdk,
        os.path.join(sdk, "upstream", "emscripten"),
        os.path.join(sdk, "upstream", "bin"),
    ]
    return [path for path in paths if os.path.isdir(path)]


def is_emsdk_ready(root_dir=None):
    root_dir = root_dir or _repo_root()
    version = read_emsdk_version(root_dir)
    sdk = emsdk_dir(root_dir)
    emcc = _emcc_path(sdk)
    if not emcc:
        return False
    try:
        result = subprocess.run(
            [emcc, "-v"],
            cwd=sdk,
            capture_output=True,
            text=True,
            check=False,
        )
        output = (result.stdout or "") + (result.stderr or "")
        return version in output
    except OSError:
        return False


def _emsdk_cmd(sdk):
    if os.name == "nt":
        return [os.path.join(sdk, "emsdk.bat")]
    return [os.path.join(sdk, "emsdk")]


def _merge_toolchain_env(sdk, env):
    env = dict(env)
    extra = _toolchain_paths(sdk)
    env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    env["EMSDK"] = sdk.replace("\\", "/")
    em_config = os.path.join(sdk, ".emscripten")
    if os.path.isfile(em_config):
        env["EM_CONFIG"] = em_config
    return env


def _capture_env(sdk):
    env = os.environ.copy()
    if os.name == "nt":
        env_script = os.path.join(sdk, "emsdk_env.bat")
        command = f'call "{env_script}" && set'
        result = subprocess.run(
            command,
            cwd=sdk,
            shell=True,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                if "=" in line and not line.startswith("PATH +"):
                    key, value = line.split("=", 1)
                    key = key.strip()
                    if key:
                        env[key] = value
        return _merge_toolchain_env(sdk, env)

    env_script = os.path.join(sdk, "emsdk_env.sh")
    command = f'. "{env_script}" && env'
    result = subprocess.run(
        ["bash", "-lc", command],
        cwd=sdk,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        for line in result.stdout.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                env[key] = value
    return _merge_toolchain_env(sdk, env)


def ensure_emsdk(root_dir=None):
    root_dir = root_dir or _repo_root()
    sdk = emsdk_dir(root_dir)
    version = read_emsdk_version(root_dir)

    if not os.path.isfile(os.path.join(sdk, "emsdk")) and not os.path.isfile(os.path.join(sdk, "emsdk.bat")):
        print("Initializing 3rd/emsdk submodule...")
        subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive", "3rd/emsdk"],
            cwd=root_dir,
            check=True,
        )

    if not os.path.isdir(sdk):
        raise FileNotFoundError(
            f"Emscripten SDK submodule is missing at {sdk}. "
            "Run: git submodule update --init --recursive"
        )

    if not is_emsdk_ready(root_dir):
        print(f"Installing Emscripten SDK {version}...")
        subprocess.run(_emsdk_cmd(sdk) + ["install", version], cwd=sdk, check=True)
        print(f"Activating Emscripten SDK {version}...")
        subprocess.run(_emsdk_cmd(sdk) + ["activate", version], cwd=sdk, check=True)

    env = _capture_env(sdk)
    emcc = shutil.which("emcc", path=env.get("PATH")) or _emcc_path(sdk)
    if not emcc:
        raise RuntimeError("emcc was not found after activating emsdk")

    result = subprocess.run(
        [emcc, "-v"],
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    output = (result.stdout or "") + (result.stderr or "")
    if version not in output:
        print(output)
        raise RuntimeError(f"Expected emcc version {version}, but activation produced a different toolchain")

    print(f"Emscripten SDK {version} is ready.")
    return env


def main():
    ensure_emsdk()


if __name__ == "__main__":
    main()
