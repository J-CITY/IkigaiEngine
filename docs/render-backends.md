# Render backends

SDF text and anti-aliased paths/shapes use the shared Renderer through the independent
[vector graphics module](vector-graphics.md). Screen-space components join `GUI`;
world-space Canvas uses a `VECTOR_WORLD` stage with a depth attachment.

Ikigai can compile several graphics APIs into one binary and pick the active API **once at startup**. Switching backends without a restart is not supported.

## Compile-time vs runtime

| Layer | Controls | Values |
| --- | --- | --- |
| CMake `USE_OPENGL` / `USE_VULKAN` / `USE_DX12` / `USE_METAL` | Which backends are **compiled in** | Defines `IKIGAI_HAS_*` and legacy `*_BACKEND` aliases |
| `/Configs/render.json` `backend` | Which compiled backend is **active** | `OPENGL`, `VULKAN`, `DIRECTX12`, `METAL` |
| CLI `--render-backend=` | Overrides JSON for this process | `opengl` / `gl`, `vulkan` / `vk`, `directx12` / `dx12`, `metal` |

If the requested backend was not compiled, startup throws with a clear error.

Copy `docs/render.json.example` to `assets/engine/Configs/render.json` (virtual path `/Configs/render.json`).

If `assets/game/Configs/render.json` exists, **it overrides the engine file**: both trees are mounted at `/`, and the game mount is added last. Edit the game config (or delete it) when the editor is OpenGL-only.

Example `/Configs/render.json` (see also `docs/render.json.example`):

```json
{
  "backend": "OPENGL"
}
```

serdepp writes the enum names (`OPENGL`, `VULKAN`, `DIRECTX12`, `METAL`). CLI aliases stay lowercase.

## Platform matrix

| Platform | Typical CMake flags |
| --- | --- |
| Windows editor | `-DUSE_OPENGL=ON -DUSE_VULKAN=ON -DUSE_DX12=ON` |
| macOS | `-DUSE_OPENGL=ON -DUSE_METAL=ON` (optional `USE_VULKAN`) |
| Emscripten | OpenGL only |

`run_vs.py -g` passes those flags for one binary. Several names compile every listed API; the process still starts a single backend from `render.json` or `--render-backend=`.

```bash
python run_vs.py -p macos -g opengl vulkan metal --build
python run_vs.py -p win -g opengl vulkan dx12 --build
```

A shipping game can still enable a single `USE_*` to keep the binary smaller.

## Startup order

1. Virtual filesystem mounts engine/user assets.
2. Load `/Configs/render.json` (missing file falls back to OpenGL).
3. Apply `--render-backend=` if present.
4. Validate the backend is compiled.
5. Create the SDL window with **one** flag set (`OPENGL` / `VULKAN` / `METAL`; DX12 uses a Win32 HWND).
6. `CreateRenderDriver` constructs the matching `DriverInterface` and sets `DriverInterface::Get()`.
7. After the driver is ready, `Window::initImGUI()` creates `IImGuiBackend`.

## ImGui

Editor and cheat UI go through `IKIGAI::IMGUI::IImGuiBackend`:

- `newFrame` / `endFrame` / `renderDrawData`
- `registerGpuTexture` for `TextureInterface::getImguiId()`
- Vulkan/DX12/Metal draw ImGui inside `Driver::submit`/`end` while a command buffer is active
- OpenGL draws ImGui in `Window::draw` then swaps

**Viewports** (`ImGuiConfigFlags_ViewportsEnable`) are enabled for OpenGL. Other backends keep docking but do not enable multi-viewport by default (platform windows are easy to get wrong on Vulkan/DX12).

## Known limits

- Game GUI sprites, FreeType labels and Spine now use the shared Renderer; the migrated pipeline has separate sprite/text shaders. Native Spine animation is not covered by the smoke scene.
- Chunk/terrain mesh upload still has a disabled GL-specific `bufferData` path.
- `debugRender.cpp` is behind `USE_EDITOR_` (legacy). The live editor uses `EditorRender`.
- Metal `CreateMesh` is not implemented yet (`CreateEmptyModel` works; mesh upload returns null).

## Smoke checks

For each compiled backend (`--render-backend=opengl|vulkan|dx12|metal`):

1. Engine starts, window flags match the API, no “not compiled” error.
2. Editor/cheats ImGui appears (docking; multi-viewport only on OpenGL).
3. File browser thumbnails and Texture Watcher show GPU textures via `getImguiId()`.
4. A simple model loads (Assimp `createBuffers` on the active backend).
5. Switching backend requires a restart; a second process can use a different API.

The tracked shader overlay and migrated/environment/cloud pipeline presets are in
[render_assets](../render_assets/README.md). Install with
`python utils/tests/assets/install_render_assets.py --activate`; this sets the `pipeline`
field in both asset render configs while preserving the selected backend.
See [shader migration validation](shader-migration-validation.md) for shader
compiler results and native GPU smoke checks.
Environment/IBL and compute cloud presets use
`python utils/tests/assets/install_render_assets.py --activate --pipeline environment-clouds`;
see [environment validation](environment-ibl-clouds-validation.md) for resource
contracts, GPU regressions and Metal/platform limitations.

Ray tracing is available on Vulkan KHR and DirectX 12 DXR through the same
`RAY_TRACE` pipeline step and reflected compute/query API. Hybrid effects and
full path tracing use explicit resources and launch dimensions. DX12 RT stages
use the same GLSL sources as Vulkan through the local SPIRV-Cross RT extension
and DXC; compute shaders follow the same path. See
[ray tracing API](ray-tracing.md) and [GPU validation](ray-tracing-validation.md).

DLSS Super Resolution and AMD FSR are optional Vulkan/DX12 providers for the
shared `UPSCALE` stage. See [upscaling](upscaling.md) for SDK setup, the
`migrated-upscale` preset, CLI/JSON options and native GPU checks.

Frame Generation has an independent `FRAME_GENERATION` presentation stage and
`migrated-frame-generation` preset. See [Frame Generation](frame-generation.md)
for Streamline/FSR runtimes, UI inputs, lifetime and validation.
