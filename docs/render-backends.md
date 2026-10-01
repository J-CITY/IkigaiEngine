# Render backends

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

- Spine (and GUI sprite components that depend on it) is OpenGL-only at runtime. Other backends log an error and skip Spine setup.
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
