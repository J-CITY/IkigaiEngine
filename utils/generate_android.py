"""Generate a Gradle Android project under android/out.

SDL3 is compiled from 3rd/SDL3 by CMake. The Java glue in
org.libsdl.app is copied from 3rd/SDL3/android-project on each run.
"""

import json
import os
import re
import shutil
import subprocess
from datetime import datetime, timezone

# SDL3's Android port requires API 21. ES 3.0 is available earlier, but the
# activity glue is not, so 3 and 3.1 share minSdk 21. ES 3.2 needs API 24.
ES_VERSIONS = {
    "3": {"cmake": 300, "min_sdk": 21, "hex": "0x00030000"},
    "3.1": {"cmake": 310, "min_sdk": 21, "hex": "0x00030001"},
    "3.2": {"cmake": 320, "min_sdk": 24, "hex": "0x00030002"},
}

DEFAULT_APP_ID = "com.ikigai.engine"
COMPILE_SDK = 35
TARGET_SDK = 35


def _read_template(repo_root, name):
    path = os.path.join(repo_root, "platform", "android", "templates", name)
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
    """AGP 8.7 needs JDK 17+. Prefer JAVA_HOME when it is new enough, else Android Studio's JBR."""
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


def generate_android(
    repo_root,
    output_dir=None,
    es_ver="3.2",
    app_id=DEFAULT_APP_ID,
    abis=None,
    use_editor=True,
    use_file_watcher=False,
):
    if es_ver not in ES_VERSIONS:
        raise ValueError("es_ver must be one of: " + ", ".join(ES_VERSIONS))

    repo_root = os.path.abspath(repo_root)
    if output_dir is None:
        output_dir = os.path.join(repo_root, "android", "out")
    output_dir = os.path.abspath(output_dir)

    if abis is None:
        abis = ["arm64-v8a", "armeabi-v7a"]
    abis = [abi.strip() for abi in abis if abi and abi.strip()]
    if not abis:
        raise ValueError("at least one ABI is required")

    if not use_editor:
        use_file_watcher = False

    sdl_project = os.path.join(repo_root, "3rd", "SDL3", "android-project")
    java_src = os.path.join(sdl_project, "app", "src", "main", "java", "org", "libsdl", "app")
    if not os.path.isdir(java_src):
        raise FileNotFoundError(
            "SDL3 Android Java glue not found at %s. Check out 3rd/SDL3." % java_src
        )

    spec = ES_VERSIONS[es_ver]
    cmake_rel = os.path.relpath(
        os.path.join(repo_root, "cmake", "android", "CMakeLists.txt"),
        os.path.join(output_dir, "app"),
    ).replace("\\", "/")
    abi_filters = ", ".join("'%s'" % abi for abi in abis)
    values = {
        "APP_NAME": "IkigaiEngine",
        "APP_ID": app_id,
        "COMPILE_SDK": COMPILE_SDK,
        "TARGET_SDK": TARGET_SDK,
        "MIN_SDK": spec["min_sdk"],
        "ANDROID_PLATFORM": "android-%d" % spec["min_sdk"],
        "GLES_CMAKE": spec["cmake"],
        "GLES_HEX": spec["hex"],
        "ABI_FILTERS": abi_filters,
        "CMAKE_PATH": cmake_rel,
        "USE_EDITOR": "ON" if use_editor else "OFF",
        "USE_FILE_WATCHER": "ON" if use_file_watcher else "OFF",
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
    shutil.copy2(
        os.path.join(sdl_project, "app", "proguard-rules.pro"),
        os.path.join(output_dir, "app", "proguard-rules.pro"),
    )

    java_dst = os.path.join(output_dir, "app", "src", "main", "java", "org", "libsdl", "app")
    shutil.copytree(java_src, java_dst, dirs_exist_ok=True)

    res_src = os.path.join(sdl_project, "app", "src", "main", "res")
    res_dst = os.path.join(output_dir, "app", "src", "main", "res")
    shutil.copytree(res_src, res_dst, dirs_exist_ok=True)

    legacy_activity = os.path.join(
        output_dir, "app", "src", "main", "java", "com", "daniil", "cross_test"
    )
    if os.path.isdir(legacy_activity):
        shutil.rmtree(legacy_activity)

    activity_dir = os.path.join(
        output_dir, "app", "src", "main", "java", *app_id.split(".")
    )
    os.makedirs(activity_dir, exist_ok=True)
    main_activity = _render(_read_template(repo_root, "MainActivity.java.in"), values)
    _write(os.path.join(activity_dir, "MainActivity.java"), main_activity)
    shutil.copy2(
        os.path.join(repo_root, "platform", "android", "static", "strings.xml"),
        os.path.join(res_dst, "values", "strings.xml"),
    )

    _write(os.path.join(output_dir, "settings.gradle"), _render(_read_template(repo_root, "settings.gradle.in"), values))
    _write(os.path.join(output_dir, "build.gradle"), _render(_read_template(repo_root, "build.gradle.in"), values))
    gradle_properties = _read_template(repo_root, "gradle.properties")
    jdk_home = _find_jdk_home()
    if jdk_home:
        gradle_properties += "org.gradle.java.home=%s\n" % jdk_home.replace("\\", "/")
    else:
        print("Warning: JDK 17+ was not found. Android Gradle Plugin 8.7 needs it (Android Studio JBR is enough).")
    _write(os.path.join(output_dir, "gradle.properties"), gradle_properties)
    _write(
        os.path.join(output_dir, "app", "build.gradle"),
        _render(_read_template(repo_root, "app.build.gradle.in"), values),
    )
    _write(
        os.path.join(output_dir, "app", "src", "main", "AndroidManifest.xml"),
        _render(_read_template(repo_root, "AndroidManifest.xml.in"), values),
    )
    run_cfg_dir = os.path.join(output_dir, ".idea", "runConfigurations")
    os.makedirs(run_cfg_dir, exist_ok=True)
    _write(
        os.path.join(run_cfg_dir, "app.xml"),
        _read_template(repo_root, "runConfiguration.xml.in"),
    )
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
        print("Warning: Android SDK not found. Set ANDROID_HOME or create android/out/local.properties.")

    _link_assets(repo_root, output_dir)

    meta = {
        "esVer": es_ver,
        "gles": spec["cmake"],
        "minSdk": spec["min_sdk"],
        "abis": abis,
        "appId": app_id,
        "generatedAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    _write(os.path.join(output_dir, ".ikigai-build.json"), json.dumps(meta, indent=2) + "\n")

    if app_id != DEFAULT_APP_ID:
        print(
            "Warning: custom applicationId %s requires matching kMainActivityJniClass in androidStorage.cpp."
            % app_id
        )

    print("Generated Android project: %s" % output_dir)
    print("OpenGL ES %s (IKIGAI_GLES_VERSION=%s, minSdk %s)" % (es_ver, spec["cmake"], spec["min_sdk"]))
    return output_dir


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Generate android/out from the IkigaiEngine Android templates.")
    parser.add_argument("--esVer", dest="es_ver", default="3.2", choices=sorted(ES_VERSIONS))
    parser.add_argument("--abi", default="arm64-v8a,armeabi-v7a")
    parser.add_argument("-o", "--output", default=None)
    args = parser.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    generate_android(
        root,
        args.output,
        es_ver=args.es_ver,
        abis=[part.strip() for part in args.abi.split(",") if part.strip()],
    )
