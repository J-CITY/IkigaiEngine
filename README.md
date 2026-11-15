# SimpleEngine
This is my sandbox project for learning computer graphics.

What's already there:

- Forward rendering with deferred rendering
- Support for custom shaders (grass, water, volumetric clouds)
- SSAO
- hdr, gamma correction
- Blinn Phong or PBD lightning
- Shadow map
- Bloom
- Motion blur
- God rays
- Skeletal animation
- Runtime plane slicing of convex [Destructible Meshes](docs/destructible-mesh.md)
- FXAA


Also support irrKlang 3d sound

## Supported Platforms & Graphics APIs

| Platform | Graphics APIs | Details / Build Flags |
| :--- | :--- | :--- |
| **Windows** | OpenGL, Vulkan, DirectX 12 | Multi-API binary (`-g opengl vulkan dx12`) |
| **Linux** | OpenGL, Vulkan | `-g opengl vulkan` |
| **macOS** | Metal, Vulkan (via MoltenVK), OpenGL | Multi-API binary (`-g opengl vulkan metal`), Xcode / Make |
| **iOS** | Metal, OpenGL ES | `-g opengl metal`, Xcode project |
| **Android** | OpenGL ES (3.0, 3.1, 3.2) | SDL3 NativeActivity, configured via `--esVer` |
| **Meta Quest** | OpenGL ES (3.0, 3.1, 3.2) | OpenXR NativeActivity + GLES |
| **Web (Emscripten)** | WebGL 2.0 (OpenGL ES 3.0) | WebAssembly build (`-p web`) |

The active backend can be selected at startup via `--render-backend=<backend>` argument or configured in `/Configs/render.json`. For more details, see [render-backends.md](docs/render-backends.md).

SDL player storage, browser persistence, Boost network dependencies and transport APIs
are described in [the profile and networking guide](docs/profile-network.md).
ZIP packs, patches and the external asset builder are covered in
[the content delivery guide](docs/content-delivery.md).

Windows builds accept independent `--dlss-sr`, `--fsr-sr`, `--dlss-fg`, and `--fsr-fg` flags. Required SDKs and missing runtimes are prepared automatically; see [Windows SR/FG builds](docs/windows-build.md).

```powershell
python run_vs.py -p win -g dx12 --dlss-sr --fsr-sr --dlss-fg --fsr-fg --build
```

Python tools use a local environment with locked dependencies. On Windows run
`.\run.cmd run_vs.py -p win -g opengl --build`; on Linux/macOS use
`sh ./run.sh run_vs.py -p linux -g opengl --build` (use `-p mac` on macOS).
The launchers prepare Python and packages automatically. Content and local test
dependencies are optional; see [Python tools](utils/PYTHON.md).

## CMake presets

CMake 3.22+ owns feature defaults and platform validation in `cmake/EngineOptions.cmake`.
The Python launcher queries those same rules before preparing SDKs. It accepts both
presets and the existing arguments:

```powershell
python run_vs.py --list-presets
python run_vs.py --preset windows -g vulkan --build -t Debug
python run_vs.py -p win -g vulkan --build -t Debug
```

Platform CMake entry points live in `cmake/<platform>/`. Both `--preset` and
`-p` use `build/<platform>/` for intermediate files and
`out/<platform>/<Debug|Release>/` for executables and APKs.
Single-configuration presets keep separate `build/<platform>/Debug` and
`Release` trees; alternate generators and core-only builds have their own subdirectories.
Android/Quest launchers generate Gradle projects in `build/android` and `build/quest`;
direct native CMake presets use their `native/` subdirectories.
`-b` overrides the build directory and `-o` overrides the output base (configuration is appended).
On Windows, the launcher installs OpenSSL through a repository-local vcpkg when
`USE_NETWORK` and `IKIGAI_NETWORK_TLS` are both enabled. Set `OPENSSL_ROOT_DIR`
to use your own installation, or pass `--skip-setup` to manage dependencies yourself.
An old cache configured from `windows/` (or another platform folder) cannot be reused
with the root project: choose a new `-b` directory. The launcher does not delete caches.
Relative paths are resolved from the repository root, even when launched elsewhere.
Use `-DNAME=VALUE` for additional CMake options, `--target NAME` for a selected target,
and `-j N` for parallel builds. Explicit options override preset values; unspecified
feature options follow the preset and existing CMake cache.

With dependencies already prepared, CMake can run directly:

```powershell
cmake --preset windows -DUSE_OPENGL=OFF -DUSE_VULKAN=ON
cmake --build --preset windows-debug
cmake --preset core-windows
cmake --build --preset core-windows-release
```

Core presets build production math, memory, time, logging, task, coroutine and render-command libraries
without graphics SDKs or `testModule`. Math exposes serde/JSON headers, so a clean
checkout needs `git submodule update --init -- 3rd/serdepp 3rd/json 3rd/nameof 3rd/magic_enum`.
Engine sources are listed explicitly in `cmake/modules/`, indexed by `cmake/ModuleManifests.cmake`;
there is no engine-source glob or regex selection. Add files to the matching module manifest
(backend files go into `renderGl`, `renderVk`, `renderDx12` or `renderMetal`). Platform selection
is shared in `cmake/PlatformSources.cmake`; feature integrations name their exclusions explicitly.
Independent libraries retain their own manifests and targets in `cmake/Math.cmake`,
`cmake/Memory.cmake`, `cmake/Coroutines.cmake`, `cmake/Time.cmake`, `cmake/Logging.cmake`, etc.
Their implementations are removed from the application lists by exact ownership.
The remaining coupled engine modules build as `ikigai_module_*` OBJECT targets through
`cmake/EngineModules.cmake`, using one snapshot of the application's platform/feature compile
requirements. This preserves static registrations and existing cross-module calls without
introducing static-library link cycles; it does not yet isolate their C++ API dependencies.
Only entry points, generated reflection and platform third-party sources stay on the application.
Bundled third-party projects retain their own source collection; this migration covers engine code.
Reflection dependencies also use the explicit header manifests, so adding a reflected header
requires adding it to its module manifest.
Memory tracking is selected with `-DIKIGAI_ENABLE_MEMORY_TRACKING=ON` and propagated
to library consumers by the memory target.
For single-config generators, configure the matching variant first, for example
`cmake --preset linux-debug` then `cmake --build --preset linux-debug`.
Use `CMakeUserPresets.json` (ignored by Git) to inherit a project preset and supply
machine-specific paths/options; the launcher also accepts those preset names.

Android/Quest CMake presets build native libraries using `ANDROID_NDK_ROOT`.
Use an NDK with C++20 stop tokens (r27 was verified). For native HTTPS/WSS, provide
OpenSSL built for the selected Android ABI through `-DOPENSSL_ROOT_DIR=...`.
Use `-DIKIGAI_NETWORK_TLS=OFF` for a build that only needs HTTP/WS.
`run_vs.py -p android` / `-p quest` prepares Gradle projects and builds APKs;
`--preset android` / `quest` also takes ABI defaults from that preset. Gradle owns
its toolchain, minimum Android API, output paths and Debug/Release variant.
`--abi` and `--esVer` remain packaging overrides. Web uses the bundled Emscripten
SDK; the launcher activates it before invoking the `web` preset.

Some screenshots

![Screenshot](https://github.com/J-CITY/SimpleEngine/blob/ecs/screenshots/editor0.png)

![Screenshot](https://github.com/J-CITY/SimpleEngine/blob/master/screenshots/0.gif)

![Screenshot](https://github.com/J-CITY/SimpleEngine/blob/master/screenshots/1.gif)

![Screenshot](https://github.com/J-CITY/SimpleEngine/blob/master/screenshots/2.gif)

![Screenshot](https://github.com/J-CITY/SimpleEngine/blob/master/screenshots/3.gif)

## Scripting

Lua and IkigaiScript are enabled by default. Use `--use-lua false` and/or
`--use-ikigai-script false` in `run_vs.py` to disable them. See
[the scripting guide](docs/scripting.md) for components, bindings and camera examples.

JSON views provide ImGui editor/debug windows with C++, Lua and IkigaiScript
controllers. Add files under the mounted `ui/windows/` directory to load windows
without rebuilding; F8 opens the reload/window manager. See
[the JSON view guide](docs/json-views.md) for the schema, examples and bindings.

## Prefabs

Scene subtrees can be saved as `.prefab.json` assets and instantiated by path.
The editor supports creation, instantiation, unpacking and undo/redo;
see [the prefab guide](docs/prefabs.md) for workflow, API and file formats.


Application startup is shared by SDL and Quest. Run `IkigaiEngine --help` for production options; optional window settings are read from `assets/game/Configs/window.json`, then overridden by explicit CLI options. ArgParse is a pinned submodule containing the reviewed fixes and is linked directly through `ArgParse::ArgParse`.

Tests and diagnostic tools live in the versioned, optional `testModule/`; local test scripts live in `utils/tests/`. Production builds do not depend on either directory. The Windows crash reporter is built separately from `testModule/tools/crashReporter` and installed beside the engine executable when crash dumps are wanted. Project coding and formatting rules are in `AGENTS.md`.
