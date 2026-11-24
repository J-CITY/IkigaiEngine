# Demo scenes

In the editor, open **Menu → Scenes** to select a demo. Physics entries are disabled when their build module is off. Reload a scene to reset its simulation; collapse the physics debug window to see the rendered objects underneath.

Enable **Window → Scene Editing** to manipulate scene objects. Click a mesh, a terrain surface or a blue marker to select it. Drag the selected surface to move it parallel to the screen. The Move and Rotate gizmos have World and Local axes. Scale follows the object's local axes. Esc cancels the current drag. Use Ctrl+Z to undo and Ctrl+Y or Ctrl+Shift+Z to redo. In the 3D Physics and Ragdoll demos, grabbing a body also selects it and shows the gizmo. Drag the body for physics or drag a gizmo handle to edit its pose. Widget Playground opens with the editor panels hidden. Use Window → Editor Panels to show them.

Run from the repository root (or beside the executable with its content links):

```powershell
.\out\windows\Debug\IkigaiEngine.exe --scene scenes/gui-demo.json
```

| Scene | Demonstration |
| --- | --- |
| `scenes/ragdoll-demo.json` | Skinned mannequin with articulated rigid bodies. Hold LMB on a limb to lift it, then release to drop it. |
| `scenes/environment-demo.json` | Tiled meadow landscape, instanced grass and wind, a pond, atmosphere/clouds, rain, wet surfaces and shelter. |
| `scenes/physics-3d-demo.json` | Pick up a box or ball with LMB. Drag from a corner to rotate the body and release to drop it. |
| `scenes/destruction-playground.json` | Click the anchored column to cut it at the hit height. Adjust the impulse and use Reset column to repeat the strike. |
| `scenes/layered-materials.json` | Switch between painted steel, bare steel and the mask. Adjust Wear and move the light to compare the surfaces. |
| `scenes/facial-expressions.json` | A stylized ceramic face with Joy, Sadness and Surprise presets and five independently editable morph targets. |
| `scenes/physics-2d-demo.json` | Visible 2D rigid bodies viewed with an orthographic camera. |
| `scenes/gui-widgets.json` | Widget Playground (800×600). Toggle the checkbox, type in the field, pick a combo option, drag the slider and scroll list, and hold the confirmation button to fill its progress bar. |
| `scenes/voxel-coast.json` | A seeded block coastline with five materials. Hold RMB and use WASD to fly, Q/E for height, Shift to accelerate. |
| `scenes/gui-demo.json` | SDF text, outlines/shadows, Bézier curves, gradients and hierarchical clipping. Use at least 800×600. |
| `scenes/particles-demo.json` | World-space billboard and mesh particles, screen-space particles and legacy emitter compatibility. |

The physics demos require the corresponding build modules. Ragdoll requires 3D physics. Enable both for the full set:

```powershell
cmake -S . -B build/windows -DUSE_PHYSICS_2D=ON -DUSE_PHYSICS_3D=ON
cmake --build build/windows --config Debug --target IkigaiEngine --parallel 6
```

These commands update an already configured Windows build. The demos use ordinary scene components and do not require `testModule`.

The environment camera uses RMB + WASD, Q/E for height, Shift for faster movement. The Environment panel opens automatically for this scene and is also available from Window > Environment. It edits weather, sun azimuth, time of day, clouds, wind and quality live without selecting an object; Save scene persists all scene changes. The landscape uses the existing tiled SurfaceComponent (80 × 80 m, terrain LOD), with up to 16,000 grass tufts batched by tile and LOD rather than separate scene objects. Grass follows the sampled terrain and fades beyond 63–75 m; the pond basin excludes grass below the waterline. The pond uses the existing transparent surface-water material, not a fluid simulation. Only the central platform and shelter provide rigid-body collision; the generated hills have no physics heightfield collider. Its dedicated render graph retains shadow maps but disables screen-space contact shadows, which produced self-shadow stripes on the landscape.

## Scene-owned rendering

Each scene can override the default render graph with a top-level `RenderPipeline` asset path:

```json
{ "RenderPipeline": "/renderPipeline/vector-sdf.json", "Objects": [] }
```

`Configs/render.json` configures the graphics backend, device/execution preferences and the default `pipeline`. Selection priority is `--pipeline` > scene `RenderPipeline` > `render.json` `pipeline` > built-in forward rendering. An absent or empty scene field uses the configured default, not the initial or previously opened scene's override. The selected initial pipeline is read before GPU initialization, including its frame-slot requirements. Switching to a graph with different frame-slot requirements requires restarting with that scene.

The older `scenes/vector-sdf-demo.json` and `scenes/particles-demo.json` virtual paths remain valid after moving their files from engine content into game content.
