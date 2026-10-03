"""Generate a Gradle Meta Quest / OpenXR project under quest/out."""

import json
import os
import re
import shutil
import subprocess
from datetime import datetime, timezone

from fetch_meta_openxr import fetch as fetch_meta_openxr, load_spec

DEFAULT_APP_ID = "com.ikigai.engine"
COMPILE_SDK = 35
TARGET_SDK = 35
MIN_SDK = 29
GLES_CMAKE = 320
GLES_HEX = "0x00030002"


def _read_template(repo_root, name):
    path = os.path.join(repo_root, "platform", "quest", "templates", name)
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def _write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def _render(text, values):
    for key, value in values.items():
        text = text.replace("{{" + key + "}}", str(value))
    return text


def _java_major(java_exe):
    try:
        proc = subprocess.run(
            [java_exe, "-version"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except OSError:
        return 0
    text = (proc.stderr or "") + (proc.stdout or "")
    match = re.search(r'version "(?:1\.)?(\d+)', text)
    if not match:
        return 0
    return int(match.group(1))


def _find_jdk_home():
    candidates = []
    java_home = os.environ.get("JAVA_HOME")
    if java_home:
        candidates.append(java_home)
    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    local = os.environ.get("LOCALAPPDATA")
    candidates.append(os.path.join(program_files, "Android", "Android Studio", "jbr"))
    if local:
        candidates.append(os.path.join(local, "Programs", "Android Studio", "jbr"))
    for home in candidates:
        java_name = "java.exe" if os.name == "nt" else "java"
        java_exe = os.path.join(home, "bin", java_name)
        if os.path.isfile(java_exe) and _java_major(java_exe) >= 17:
            return home
    return None


def _find_sdk():
    for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        value = os.environ.get(key)
        if value and os.path.isdir(value):
            return value
    home = os.path.expanduser("~")
    local = os.environ.get("LOCALAPPDATA")
    candidates = []
    if local:
        candidates.append(os.path.join(local, "Android", "Sdk"))
    candidates.append(os.path.join(home, "Android", "Sdk"))
    candidates.append(os.path.join(home, "Library", "Android", "sdk"))
    for path in candidates:
        if os.path.isdir(path):
            return path
    return None


def _link_assets(repo_root, output_dir):
    src = os.path.abspath(os.path.join(repo_root, "assets"))
    dest = os.path.abspath(os.path.join(output_dir, "app", "src", "main", "assets"))
    if not os.path.isdir(src) or os.path.lexists(dest):
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.name == "nt":
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", dest, src],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        os.symlink(src, dest, target_is_directory=True)


def _copy_placeholder_icon(res_dst):
    densities = ("mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi")
    for density in densities:
        mipmap = os.path.join(res_dst, "mipmap-%s" % density)
        os.makedirs(mipmap, exist_ok=True)


def generate_quest(
    repo_root,
    output_dir=None,
    app_id=DEFAULT_APP_ID,
    abis=None,
):
    repo_root = os.path.abspath(repo_root)
    if output_dir is None:
        output_dir = os.path.join(repo_root, "quest", "out")
    output_dir = os.path.abspath(output_dir)

    if abis is None:
        abis = ["arm64-v8a"]
    abis = [abi.strip() for abi in abis if abi and abi.strip()]
    if not abis:
        raise ValueError("at least one ABI is required")

    openxr_headers = os.path.join(repo_root, "3rd", "OpenXR-SDK", "include", "openxr", "openxr.h")
    if not os.path.isfile(openxr_headers):
        raise FileNotFoundError(
            "OpenXR-SDK headers not found at %s. Run: git submodule update --init 3rd/OpenXR-SDK"
            % openxr_headers
        )

    spec = load_spec(repo_root)
    fetch_meta_openxr(repo_root)

    sdl_project = os.path.join(repo_root, "3rd", "SDL3", "android-project")
    if not os.path.isdir(os.path.join(sdl_project, "gradle", "wrapper")):
        raise FileNotFoundError(
            "Gradle wrapper not found at %s. Check out 3rd/SDL3." % sdl_project
        )

    cmake_rel = os.path.relpath(
        os.path.join(repo_root, "cmake", "quest", "CMakeLists.txt"),
        os.path.join(output_dir, "app"),
    ).replace("\\", "/")
    jni_libs = os.path.relpath(
        os.path.join(repo_root, "3rd", "MetaOpenXR", "OpenXR", "Libs", "Android"),
        os.path.join(output_dir, "app"),
    ).replace("\\", "/")
    abi_filters = ", ".join("'%s'" % abi for abi in abis)
    values = {
        "APP_NAME": "IkigaiEngine",
        "APP_ID": app_id,
        "COMPILE_SDK": COMPILE_SDK,
        "TARGET_SDK": TARGET_SDK,
        "MIN_SDK": MIN_SDK,
        "ANDROID_PLATFORM": "android-%d" % MIN_SDK,
        "GLES_CMAKE": GLES_CMAKE,
        "GLES_HEX": GLES_HEX,
        "ABI_FILTERS": abi_filters,
        "CMAKE_PATH": cmake_rel,
        "JNI_LIBS": jni_libs,
        "OPENXR_LOADER_VERSION": spec["loader"]["version"],
    }

    os.makedirs(output_dir, exist_ok=True)

    wrapper_dst = os.path.join(output_dir, "gradle", "wrapper")
    os.makedirs(wrapper_dst, exist_ok=True)
    for name in ("gradlew", "gradlew.bat"):
        shutil.copy2(os.path.join(sdl_project, name), os.path.join(output_dir, name))
    wrapper_src = os.path.join(sdl_project, "gradle", "wrapper")
    for name in os.listdir(wrapper_src):
        src = os.path.join(wrapper_src, name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(wrapper_dst, name))

    os.makedirs(os.path.join(output_dir, "app"), exist_ok=True)
    proguard_src = os.path.join(sdl_project, "app", "proguard-rules.pro")
    if os.path.isfile(proguard_src):
        shutil.copy2(proguard_src, os.path.join(output_dir, "app", "proguard-rules.pro"))
    else:
        _write(os.path.join(output_dir, "app", "proguard-rules.pro"), "")

    res_dst = os.path.join(output_dir, "app", "src", "main", "res")
    os.makedirs(os.path.join(res_dst, "values"), exist_ok=True)
    shutil.copy2(
        os.path.join(repo_root, "platform", "quest", "static", "strings.xml"),
        os.path.join(res_dst, "values", "strings.xml"),
    )
    android_res = os.path.join(sdl_project, "app", "src", "main", "res")
    if os.path.isdir(android_res):
        for name in os.listdir(android_res):
            if name.startswith("mipmap") or name.startswith("drawable"):
                src = os.path.join(android_res, name)
                dst = os.path.join(res_dst, name)
                if os.path.isdir(src):
                    shutil.copytree(src, dst, dirs_exist_ok=True)
    _copy_placeholder_icon(res_dst)

    activity_dir = os.path.join(output_dir, "app", "src", "main", "java", *app_id.split("."))
    os.makedirs(activity_dir, exist_ok=True)
    _write(os.path.join(activity_dir, "MainActivity.java"), _render(_read_template(repo_root, "MainActivity.java.in"), values))

    _write(os.path.join(output_dir, "settings.gradle"), _render(_read_template(repo_root, "settings.gradle.in"), values))
    _write(os.path.join(output_dir, "build.gradle"), _render(_read_template(repo_root, "build.gradle.in"), values))
    gradle_properties = _read_template(repo_root, "gradle.properties")
    jdk_home = _find_jdk_home()
    if jdk_home:
        gradle_properties += "org.gradle.java.home=%s\n" % jdk_home.replace("\\", "/")
    else:
        print("Warning: JDK 17+ was not found. Android Gradle Plugin 8.7 needs it (Android Studio JBR is enough).")
    _write(os.path.join(output_dir, "gradle.properties"), gradle_properties)
    _write(os.path.join(output_dir, "app", "build.gradle"), _render(_read_template(repo_root, "app.build.gradle.in"), values))
    _write(
        os.path.join(output_dir, "app", "src", "main", "AndroidManifest.xml"),
        _render(_read_template(repo_root, "AndroidManifest.xml.in"), values),
    )
    run_cfg_dir = os.path.join(output_dir, ".idea", "runConfigurations")
    os.makedirs(run_cfg_dir, exist_ok=True)
    _write(os.path.join(run_cfg_dir, "app.xml"), _read_template(repo_root, "runConfiguration.xml.in"))
    _write(
        os.path.join(output_dir, ".gitignore"),
        "\n".join([
            ".gradle/",
            "local.properties",
            ".idea/",
            "build/",
            "app/build/",
            "app/.cxx/",
            "app/.externalNativeBuild/",
            "captures/",
            "*.iml",
            "",
        ]),
    )

    sdk = _find_sdk()
    if sdk:
        sdk_escaped = sdk.replace("\\", "\\\\")
        _write(os.path.join(output_dir, "local.properties"), "sdk.dir=%s\n" % sdk_escaped)
    else:
        print("Warning: Android SDK not found. Set ANDROID_HOME or create quest/out/local.properties.")

    _link_assets(repo_root, output_dir)

    meta = {
        "platform": "quest",
        "gles": GLES_CMAKE,
        "minSdk": MIN_SDK,
        "abis": abis,
        "appId": app_id,
        "openxrLoader": spec["loader"]["version"],
        "generatedAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    _write(os.path.join(output_dir, ".ikigai-build.json"), json.dumps(meta, indent=2) + "\n")

    print("Generated Quest project: %s" % output_dir)
    print("OpenXR loader %s, GLES 3.2, minSdk %s, ABI %s" % (
        spec["loader"]["version"], MIN_SDK, ",".join(abis)
    ))
    return output_dir


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Generate quest/out from the IkigaiEngine Quest templates.")
    parser.add_argument("--abi", default="arm64-v8a")
    parser.add_argument("-o", "--output", default=None)
    args = parser.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    generate_quest(
        root,
        args.output,
        abis=[part.strip() for part in args.abi.split(",") if part.strip()],
    )
