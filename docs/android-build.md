# Android build

`python run_vs.py -p android` writes a Gradle project to `android/out`. Open that folder in Android Studio, or build it with `--build`.

```text
python run_vs.py -p android
python run_vs.py -p android --esVer 3.1 --build
python run_vs.py -p android --esVer 3.2 --abi arm64-v8a --build
```

## OpenGL ES

`--esVer` selects the context SDL creates and the GLES headers the engine compiles against. The default is **3.2**.

| `--esVer` | `IKIGAI_GLES_VERSION` | `minSdk` | Context | Headers |
|-----------|----------------------|----------|---------|---------|
| `3` | 300 | 21 | ES 3.0 | `GLES3/gl3.h` |
| `3.1` | 310 | 21 | ES 3.1 | `GLES3/gl31.h` |
| `3.2` | 320 | 24 | ES 3.2 | `GLES3/gl32.h` |

SDL3's Java activity requires API 21, so ES 3.0 does not drop `minSdk` to 18. ES 3.2 needs API 24. The manifest `glEsVersion` matches the same choice.

Changing `--esVer` regenerates `android/out` and passes `-DIKIGAI_GLES_VERSION` into CMake. Gradle reconfigures the native build from that argument.

## What is generated

- Gradle wrapper copied from `3rd/SDL3/android-project` (AGP 8.7.3).
- `org.libsdl.app` Java sources copied from that same tree, so they stay in lockstep with `3rd/SDL3`.
- `MainActivity` (`com.ikigai.engine`) loads `libSDL3.so` and `libmain.so`.
- Native code is `cmake/android/CMakeLists.txt`, which builds SDL3 with `add_subdirectory` and the engine as the shared library `main`.
- `assets/` is junctioned or symlinked to `android/out/app/src/main/assets` when the assets directory exists.
- `android/out/.ikigai-build.json` records the ES version used for the last generation.

Do not edit files under `android/out` by hand. Change `platform/android/templates`, `cmake/IkigaiEngineAndroid.cmake`, or `3rd/SDL3`, then run the generator again.

## Troubleshooting launch

If Logcat or the installer shows  
`Activity class {com.daniil.cross_test/com.ikigai.engine.MainActivity} does not exist`,  
Android Studio is still using a **stale run configuration** from the old `applicationId`:

1. **File → Sync Project with Gradle Files**
2. **Run → Edit Configurations…** → **app** → **Launch Options**: **Default Activity** (not a hard-coded class from the old package)
3. Uninstall old APKs: `com.daniil.cross_test` and reinstall `com.ikigai.engine`
4. Re-run `python run_vs.py -p android` to refresh `android/out` (includes `.idea/runConfigurations/app.xml`)

## Requirements

- Android SDK (compile/target 35). `ANDROID_HOME` or the default SDK path is written into `android/out/local.properties` when it can be found.
- Android NDK and CMake installed through the SDK. Gradle picks them up; a missing NDK fails at configure time.
- JDK 17 or newer (required by Android Gradle Plugin 8.7). If `java` on PATH is older, the generator writes `org.gradle.java.home` to Android Studio's bundled JBR when that JBR is installed.
- A checkout of `3rd/SDL3`, including `android-project`.

## Native layout

`cmake/IkigaiEngineAndroid.cmake` builds assimp, freetype, glslang, SPIRV-Cross, ImGui, Spine, Lua, and SoLoud as static libraries, and SDL3 as `libSDL3.so`. The game library is `libmain.so`, which is the name `SDLActivity` loads by default.

`USING_GLES` stays defined. `--esVer 3.1` and `3.2` use real shader storage buffers (`GL_SHADER_STORAGE_BUFFER`, GLSL ES 310/320). `--esVer 3` keeps the ES 3.0 path: storage blocks are emitted as uniform buffers.
