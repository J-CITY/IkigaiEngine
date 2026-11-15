# Movement Animset Pro: extracted source assets

Extracted from the user's local `Movement Animset Pro v1.693.unitypackage`.
The source archive was preserved. Files retain their original Unity asset paths.

Contents: 13 FBX files (118,175,008 bytes), with their original `.fbx.meta` files:

- 7 main animation bundles in `Assets/MovementAnimsetPro/Animations/`.
- 1 auxiliary face animation bundle.
- 3 original backup animation bundles in `MovementAnimsetPro_SlideClimb.bck/`.
- 2 model files (`Character.fbx`, `Dummy.fbx`).

Useful clips for the third-person demo:

| FBX bundle | Clips |
| --- | --- |
| MovementAnimsetPro | Idle, RunFwdLoop, jump starts and landings, FallingLoop |
| MovementAnimsetPro_RunStrafeUpdate | RunBwdLoop, RunLtLoop, RunRtLoop, RunStrafeLeft45Loop, RunStrafeRight45Loop, RunStrafeLeft135Loop, RunStrafeRight135Loop, Climb1m |
| MovementAnimsetPro_SprintFixed | SprintFwdLoop |
| MovementAnimsetPro_SlideClimb | Slide, Vault1m, Climb2m |
| MovementAnimsetPro_Additionals | WalkBwdLoop, StrafeLeftLoop, StrafeRightLoop |

The main animation FBX files have 74 LimbNode bones and multiple animation takes.
They require take selection during import and retargeting to the demo's Mixamo
character; they have not yet been connected to the runtime animation manifest.
This package has a forward sprint clip; directional running clips are separate.

`animation-catalog.json` records actual FBX takes, bone names, and Unity clip frame
ranges. `extraction-manifest.json` records the source package and extracted sizes.
Unity metadata is preserved as reference; the engine does not consume it directly.
