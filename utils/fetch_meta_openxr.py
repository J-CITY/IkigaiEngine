"""Download the Khronos Android OpenXR loader into 3rd/MetaOpenXR.

Meta Quest no longer requires a proprietary loader. The official path is
org.khronos.openxr:openxr_loader_for_android (1.0.34+). Headers come from
the 3rd/OpenXR-SDK git submodule.
"""

import json
import os
import shutil
import sys
import urllib.request
import zipfile


def repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def version_path(root):
    return os.path.join(root, "platform", "quest", "meta_openxr_version.json")


def dest_root(root):
    return os.path.join(root, "3rd", "MetaOpenXR")


def marker_path(root):
    return os.path.join(dest_root(root), "version.txt")


def load_spec(root):
    with open(version_path(root), "r", encoding="utf-8") as handle:
        return json.load(handle)


def loader_ready(root, spec):
    version = spec["loader"]["version"]
    marker = marker_path(root)
    if not os.path.isfile(marker):
        return False
    with open(marker, "r", encoding="utf-8") as handle:
        if handle.read().strip() != version:
            return False
    so_path = os.path.join(
        dest_root(root), "OpenXR", "Libs", "Android", "arm64-v8a", "libopenxr_loader.so"
    )
    return os.path.isfile(so_path)


def _download(url, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    print("Downloading %s" % url)
    local_aar = os.environ.get("META_OPENXR_AAR")
    if local_aar and os.path.isfile(local_aar):
        shutil.copy2(local_aar, dest)
        return
    with urllib.request.urlopen(url) as response, open(dest, "wb") as out:
        shutil.copyfileobj(response, out)


def fetch(root=None, force=False):
    root = os.path.abspath(root or repo_root())
    spec = load_spec(root)
    if not force and loader_ready(root, spec):
        print("OpenXR Android loader %s already present." % spec["loader"]["version"])
        return dest_root(root)

    dest = dest_root(root)
    cache = os.path.join(dest, "_cache")
    os.makedirs(cache, exist_ok=True)
    aar_path = os.path.join(cache, "%s-%s.aar" % (spec["loader"]["artifact"], spec["loader"]["version"]))
    _download(spec["loader"]["url"], aar_path)

    extract_dir = os.path.join(cache, "aar")
    if os.path.isdir(extract_dir):
        shutil.rmtree(extract_dir)
    os.makedirs(extract_dir, exist_ok=True)
    with zipfile.ZipFile(aar_path, "r") as archive:
        archive.extractall(extract_dir)

    jni_dir = os.path.join(extract_dir, "jni")
    if not os.path.isdir(jni_dir):
        raise FileNotFoundError("AAR has no jni/ folder: %s" % aar_path)

    libs = os.path.join(dest, "OpenXR", "Libs", "Android")
    if os.path.isdir(libs):
        shutil.rmtree(libs)
    copied = 0
    for abi in os.listdir(jni_dir):
        src_so = os.path.join(jni_dir, abi, "libopenxr_loader.so")
        if not os.path.isfile(src_so):
            continue
        abi_dir = os.path.join(libs, abi)
        os.makedirs(abi_dir, exist_ok=True)
        shutil.copy2(src_so, os.path.join(abi_dir, "libopenxr_loader.so"))
        for config in ("Debug", "Release"):
            config_dir = os.path.join(abi_dir, config)
            os.makedirs(config_dir, exist_ok=True)
            shutil.copy2(src_so, os.path.join(config_dir, "libopenxr_loader.so"))
        copied += 1
    if copied == 0:
        raise FileNotFoundError("No libopenxr_loader.so found in %s" % jni_dir)

    include_src = os.path.join(extract_dir, "prefab", "modules", "openxr_loader", "include")
    include_dst = os.path.join(dest, "OpenXR", "Include")
    if os.path.isdir(include_src):
        if os.path.isdir(include_dst):
            shutil.rmtree(include_dst)
        shutil.copytree(include_src, include_dst)

    with open(marker_path(root), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(spec["loader"]["version"] + "\n")

    print("Installed OpenXR Android loader %s into %s (%d ABI(s))" % (
        spec["loader"]["version"], dest, copied
    ))
    return dest


def require(root=None):
    root = os.path.abspath(root or repo_root())
    spec = load_spec(root)
    if loader_ready(root, spec):
        return dest_root(root)
    print("Meta/Khronos OpenXR loader is missing. Run: python utils/fetch_meta_openxr.py")
    return fetch(root)


if __name__ == "__main__":
    force = "--force" in sys.argv
    fetch(force=force)
