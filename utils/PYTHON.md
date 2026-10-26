# Python tools

Use the project launcher from the repository root. Python and package installation
are automatic; activating a virtual environment is unnecessary.

```powershell
# Windows: configure and optionally build the engine.
.\run.cmd run_vs.py -p win -g opengl --build

# Set up native dependencies without configuring an engine build.
.\run.cmd utils/setup_env.py win

# Download assets only when needed.
.\run.cmd utils/assets/sync_assets.py ensure

# Include Pillow for content archives with texture atlases.
.\run.cmd --content utils/build/resource_builder/build_content.py --help

# Include the local validation dependencies.
.\run.cmd --tests utils/tests/test_tool_layout.py
.\run.cmd --tests utils/tests/assets/build_render_smoke_assets.py
.\run.cmd --tests utils/tests/assets/install_render_assets.py
```

On Linux/macOS use `sh ./run.sh` with the same arguments, for example:

```sh
sh ./run.sh run_vs.py -p linux -g opengl --build
sh ./run.sh --tests utils/tests/test_tool_layout.py
```

No arguments display the build tool's help. The launcher switches to the repository
root, so script paths and build arguments are relative to the repository. Existing
`python run_vs.py`, `python utils/setup_env.py` and asset-sync entry points also
delegate to this environment, except when displaying help. Imported modules do
not create environments or start downloads.

## Files and dependencies

| Path | Purpose | Versioned |
| --- | --- | --- |
| `pyproject.toml` | Python compatibility and direct dependency groups | Yes |
| `.python-version` | Python 3.12 selection; uv chooses an available patch release | Yes |
| `uv.lock` | Resolved package versions and archive hashes | Yes |
| `.venv/` | Installed project packages | No |
| `.python/` | Downloaded Python installations | No |
| `utils/.pack/uv-cache/` | Python/package download cache | No |
| `utils/.pack/uv-tools/` | uv installer and binary, when uv is absent from PATH | No |

The base environment contains `gdown` for public Google Drive assets and `certifi`
for HTTPS certificates. `--content` adds Pillow. `--tests` adds Pillow, NumPy and
jsonschema and enables the content extra. Local tests and their generators stay
under ignored `utils/tests/`; production tools do not import that directory.

`utils/assets/` contains the production asset synchronization tool. Scene, demo,
pipeline and fixture generators, their shared `scene_helpers.py` and the local
`install_render_assets.py` live in `utils/tests/assets/`. The installer runs
automatically only for the optional test runner. Generated asset destinations
remain `render_assets/` and `assets/`.

The launcher uses uv from PATH, or installs it locally using Astral's official
installer with PATH modification disabled. First use requires network access.
Subsequent runs reuse the local interpreter, packages and cache. An unrelated
activated environment and user site packages do not affect project tools.

CMake uses the interpreter passed by `run_vs.py`. Direct CMake configuration
prefers `.venv` when present and no interpreter was explicitly selected. The same
host interpreter handles reflection generation on desktop and cross builds.
CMake does not install Python packages. C++ compilers, platform SDKs and other
native dependencies are still prepared by the existing platform setup.

## Updating dependencies

Edit `pyproject.toml`, then update the lock using the same local paths:

```powershell
$env:UV_CACHE_DIR = "$PWD/utils/.pack/uv-cache"
$env:UV_PYTHON_INSTALL_DIR = "$PWD/.python"
uv lock
```

```sh
UV_CACHE_DIR="$PWD/utils/.pack/uv-cache" UV_PYTHON_INSTALL_DIR="$PWD/.python" uv lock
```

If uv was bootstrapped locally, use `utils/.pack/uv-tools/uv.exe` on Windows or
`utils/.pack/uv-tools/uv` on Linux/macOS instead of `uv`. Commit the manifest and
lock together. Launchers use `--locked` and fail if the manifest and lock disagree.
