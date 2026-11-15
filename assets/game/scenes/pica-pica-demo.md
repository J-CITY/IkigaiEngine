# Pica Pica — Hybrid PBR

Open **Menu → Scenes → Pica Pica - Hybrid PBR**. For the full ray-traced
version, start a Vulkan build with a ray tracing capable GPU:

```powershell
windows/outRT/Release/IkigaiEngine.exe --render-backend=vulkan --scene=/scenes/pica-pica-demo.json
```

The scene selects `/renderPipeline/pica-pica-demo.json`, whose `SettingsAsset`
is `/environments/pica-pica-demo.json`. An explicit `--pipeline` takes precedence.
Other scenes without `RenderPipeline` use the startup pipeline.

Hold **right mouse button** to control the camera. **WASD** moves, **Q/E** moves
down/up, **Left Shift** accelerates. Release RMB to use the editor. Window
focus and ImGui mouse/keyboard capture suspend the controller.

Deferred PBR, per-light RT shadows (one sun, two point lights), RTAO,
RT reflections and RT diffuse GI use the engine's hybrid render graph.
RT AO/reflections/GI use temporal denoising; TAA resolves before ACES tone mapping.
The environment supplies the sky and precomputed diffuse/specular IBL.
The demo traces opaque shadows, AO, reflections and GI at native resolution.
Shadows use eight samples, AO four, reflections and GI two, with edge-aware
spatial filtering (3x3 shadows, 5x5 AO/reflections/GI). AO/reflections/GI
accumulate 95% temporal history with depth/normal-validated bilinear reprojection.
Each transparent layer has independent visibility at half resolution. Shadows
use spatial filtering per light; their scratch targets cannot hold independent
light histories. Opaque direct lighting is accumulated after all three lights
are resolved, giving shadow noise a single consistent temporal history.
Native TAA uses 95% history and motion vectors with projection jitter removed,
so the sampling sequence does not shake a stationary view. RT reprojection adds
the jitter delta back because its histories follow the G-buffer sampling grid.
Ray origins
use an exact full-resolution G-buffer sample to avoid self-shadowing artifacts.
Geometry, transparency, TAA and tone mapping keep the window resolution.
Unused volumetric fog, god rays and bloom passes and targets are omitted.
The light pass names `RTShadowRawTrace` as its `ShadowProviderStage`. When that
program is available, raster shadow captures are skipped; all opaque and
transparent direct-light consumers use their corresponding RT visibility.
Unsupported RT preserves raster shadow captures and the material fallback.
Unsupported RT stages use the graph's raster/IBL fallbacks and report this in
the log; this mode does not provide the full ray-traced result.

For interactive performance, build and run **Release**. Debug includes Vulkan
synchronization validation and unoptimized CPU code:

```powershell
cmake --build windows/buildRT --config Release --target IkigaiEngine --parallel 6
windows/outRT/Release/IkigaiEngine.exe --render-backend=vulkan --scene=/scenes/pica-pica-demo.json
```

For a repeatable frame-time measurement, add `--smoke-frames=96
--smoke-size=1280x720 --smoke-benchmark --smoke-log=utils/.pack/pica-benchmark.log`.
The benchmark excludes loading, four warm-up frames and the final capture frame.
For temporal diagnostics, `--smoke-readbacks=Display,Velocity,RTAOFiltered,RTGIFiltered
--smoke-readback-frames=8 --smoke-readback-dir=utils/.pack/pica-temporal` also
exports the last eight frames into separate directories. GPU readbacks stall
the renderer; benchmark FPS using a run without intermediate readbacks.

Launching without arguments and choosing **Menu → Scenes → Pica Pica - Hybrid
PBR** uses the same scene pipeline. The menu bar displays measured FPS and frame
time. File watching checks native metadata asynchronously every five seconds;
it does not wait for filesystem I/O inside a rendered frame.

`diorama.model` bakes the original glTF node transforms at import. All 28
materials retain their order, base color factors, metallic/roughness factors,
available textures, double-sidedness and the glass alpha mode. The demo's
G-buffer and transparent capture shaders sample the glTF metallic/roughness
texture in linear space (G = roughness, B = metallic). Secondary RT hits use
the engine's scalar roughness/metallic provider and alpha-tested visibility;
the glTF transmission extension is approximated by alpha transparency.

Regenerate the scene/material/pipeline descriptors after installing engine
render assets with `python utils/assets/build_pica_pica_demo.py`. The generator
does not modify the original model or other scenes. The model attribution and
license are in `../models/pica_pica/license.txt`.
