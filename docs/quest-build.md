# Meta Quest (OpenXR) build

`python run_vs.py -p quest` writes a Gradle project to `quest/out`. Open that folder in Android Studio, or build it with `--build`.

```text
python run_vs.py -p quest
python run_vs.py -p quest --abi arm64-v8a --build
```

The old `oculus/gl2tri3dOXR` tree is a reference sample only. Do not generate or build from it.

## OpenXR

Quest uses:

- **Headers:** git submodule `3rd/OpenXR-SDK` (Khronos).
- **Loader:** Khronos Android loader `org.khronos.openxr:openxr_loader_for_android` (Meta-supported, 1.0.34+). `utils/fetch_meta_openxr.py` downloads the AAR into `3rd/MetaOpenXR/` (gitignored). Version is pinned in `platform/quest/meta_openxr_version.json`.

```text
python utils/fetch_meta_openxr.py
python utils/fetch_meta_openxr.py --force
```

Set `META_OPENXR_AAR` to a local `.aar` if the Maven download is blocked.

## What is generated

- Gradle wrapper copied from `3rd/SDL3/android-project` (AGP 8.7.3).
- `MainActivity` (`com.ikigai.engine`) is a `NativeActivity` that loads `libopenxr_loader.so` and `libmain.so`.
- Native code is `cmake/quest/CMakeLists.txt` (`OCULUS`, OpenGL ES 3.2, no SDL, no editor).
- `assets/` is junctioned or symlinked to `quest/out/app/src/main/assets` when the assets directory exists.
- Manifest has `com.oculus.intent.category.VR` and `android.hardware.vr.headtracking`.
- `quest/out/.ikigai-build.json` records the last generation.

Do not edit files under `quest/out` by hand. Change `platform/quest/templates`, `cmake/IkigaiEngineQuest.cmake`, or the fetch spec, then run the generator again.

## Controllers

Touch controllers are mapped through OpenXR actions into the existing `InputManager` gamepad events:

- Combined virtual pad id `0` matches `<Gamepad>` in `/Configs/input.input` (`buttonSouth` = A, left stick = left thumbstick, triggers / grips as shoulders).
- Left hand is also pad `1`, right hand pad `2`, if a script queries them by id.

## Requirements

- Android SDK (compile/target 35). `ANDROID_HOME` or the default SDK path is written into `quest/out/local.properties`.
- Android NDK and CMake through the SDK.
- JDK 17 or newer (AGP 8.7).
- `3rd/OpenXR-SDK` submodule and a checkout of `3rd/SDL3` (Gradle wrapper only).
- A Meta Quest headset in developer mode.

## Native layout

`USING_GLES` stays defined. The XR swapchain FBO is bound per eye; `GameRenderer::renderSceneOculus` draws the scene twice with OpenXR view / projection matrices.
