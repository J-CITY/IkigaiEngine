"""Sync local ./assets with the public Google Drive source.

Pull needs no Google account: the Drive folder/file must be readable by link.
Push needs rclone on this machine (OAuth stays in the local rclone config).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import site
import subprocess
import sys
import tempfile
import time
import zipfile

STAMP_NAME = ".ikigai-assets-stamp.json"
SKIP_NAMES = {STAMP_NAME, "desktop.ini", ".DS_Store", "Thumbs.db"}
DEFAULT_ZIP_NAME = "IkigaiEngine-assets.zip"
SOURCE_KEYS = (
    "mode",
    "driveFolderId",
    "shareUrl",
    "localPath",
    "zipFileName",
    "driveFileId",
    "sha256",
    "version",
)


def repo_root(root_dir=None):
    if root_dir:
        return os.path.abspath(root_dir)
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def source_path(root_dir):
    return os.path.join(root_dir, "utils", "assets.source.json")


def remote_path(root_dir):
    return os.path.join(root_dir, "utils", "assets.remote.json")


def pack_dir(root_dir):
    return os.path.join(root_dir, "utils", ".pack")


def read_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path, data):
    text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def load_source(root_dir):
    path = source_path(root_dir)
    if not os.path.isfile(path):
        raise FileNotFoundError(f"Missing {path}")
    data = read_json(path)
    if "mode" not in data:
        raise ValueError("assets.source.json is missing 'mode'.")
    return data


def save_source(root_dir, source):
    payload = {key: source.get(key, "" if key != "version" else 0) for key in SOURCE_KEYS}
    payload["version"] = int(payload.get("version") or 0)
    write_json(source_path(root_dir), payload)


def source_get(source, name, default=""):
    value = source.get(name, default)
    if value is None:
        return default
    return value


def source_identity(source):
    return {
        "mode": (source_get(source, "mode", "folder") or "folder").lower(),
        "version": int(source_get(source, "version", 0) or 0),
        "sha256": str(source_get(source, "sha256")).lower(),
        "driveFolderId": str(source_get(source, "driveFolderId")),
        "driveFileId": str(source_get(source, "driveFileId")),
    }


def assert_mode(source):
    mode = source_identity(source)["mode"]
    if mode not in ("folder", "zip"):
        raise ValueError(f"assets.source.json mode must be 'folder' or 'zip' (got {mode!r}).")
    return mode


def local_assets_dir(root_dir, source):
    rel = str(source_get(source, "localPath", "assets") or "assets")
    return os.path.join(root_dir, rel)


def stamp_path(assets_dir):
    return os.path.join(assets_dir, STAMP_NAME)


def read_stamp(assets_dir):
    path = stamp_path(assets_dir)
    if not os.path.isfile(path):
        return None
    try:
        return read_json(path)
    except (OSError, json.JSONDecodeError):
        return None


def write_stamp(assets_dir, source):
    os.makedirs(assets_dir, exist_ok=True)
    write_json(stamp_path(assets_dir), source_identity(source))


def iter_asset_files(root):
    if not os.path.isdir(root):
        return
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [name for name in dirnames if name not in {".git"}]
        for name in filenames:
            if name in SKIP_NAMES:
                continue
            yield os.path.join(dirpath, name)


def has_asset_files(assets_dir):
    return next(iter_asset_files(assets_dir), None) is not None


def same_origin(left, right):
    return (
        left["mode"] == right["mode"]
        and left["driveFolderId"] == right["driveFolderId"]
        and left["driveFileId"] == right["driveFileId"]
    )


def assets_status(root_dir=None, source=None):
    """Return (state, reason) for the local assets tree vs assets.source.json.

    state: missing | current | stale | local-newer | unmarked
    """
    root_dir = repo_root(root_dir)
    source = source if source is not None else load_source(root_dir)
    dest = local_assets_dir(root_dir, source)
    ident = source_identity(source)
    if not has_asset_files(dest):
        return "missing", "local assets are missing or empty"
    stamp = read_stamp(dest)
    if stamp is None:
        if ident["version"] > 0 or ident["sha256"]:
            return "stale", "no local stamp; source.json has a version or hash"
        return "unmarked", "local assets exist but have no stamp"
    stamp_ident = source_identity(stamp)
    if stamp_ident == ident:
        return "current", "local stamp matches assets.source.json"
    if stamp_ident["version"] > ident["version"] and same_origin(stamp_ident, ident):
        return "local-newer", f"local version {stamp_ident['version']} > source {ident['version']}"
    return "stale", "assets.source.json is newer than the local stamp"


def folder_url(source):
    url = str(source_get(source, "shareUrl")).strip()
    if url:
        return url
    folder_id = str(source_get(source, "driveFolderId")).strip()
    if not folder_id:
        raise ValueError("assets.source.json needs driveFolderId or shareUrl for folder mode.")
    return f"https://drive.google.com/drive/folders/{folder_id}"


def zip_name(source):
    name = str(source_get(source, "zipFileName", DEFAULT_ZIP_NAME) or DEFAULT_ZIP_NAME)
    return name


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_gdown():
    try:
        import gdown  # noqa: F401

        return
    except ImportError:
        pass

    check_pip = subprocess.run(
        [sys.executable, "-m", "pip", "--version"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if check_pip.returncode != 0:
        raise RuntimeError(
            "Python module 'pip' is not installed.\n"
            "Please install pip via your package manager:\n"
            "  Arch / Manjaro: sudo pacman -S python-pip\n"
            "  Ubuntu / Debian: sudo apt install python3-pip\n"
            "Or pass --skip-setup (-S) to run_vs.py to skip asset downloading for now."
        )

    print("Installing gdown (once, no Google account)...")
    res = subprocess.run(
        [sys.executable, "-m", "pip", "install", "--user", "gdown"],
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        if "externally-managed-environment" in res.stderr or "PEP 668" in res.stderr:
            subprocess.run(
                [sys.executable, "-m", "pip", "install", "--user", "--break-system-packages", "gdown"],
                check=True,
            )
        else:
            sys.stderr.write(res.stderr)
            res.check_returncode()

    user_site = site.getusersitepackages()
    if user_site not in sys.path:
        sys.path.append(user_site)
    import importlib

    importlib.invalidate_caches()
    import gdown  # noqa: F401


def _drive_quota_error(exc):
    text = str(exc).lower()
    return (
        "many accesses" in text
        or "cannot retrieve the public link" in text
        or "failed to retrieve file url" in text
    )


# gdown's default file user-agent is an old Chrome build. Drive answers that
# client with an HTML page the parser treats as a quota/permission error.
# Folder listing already uses a current browser agent; file downloads must too.
# Cookies stay off: a stale ~/.cache/gdown/cookies.txt produces the same page.
DRIVE_USER_AGENT = (
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/98.0.4758.102 Safari/537.36"
)


def download_drive_file(file_id, dest_file, attempts=5):
    import gdown
    from gdown.exceptions import FileURLRetrievalError

    os.makedirs(os.path.dirname(os.path.abspath(dest_file)), exist_ok=True)
    url = f"https://drive.google.com/uc?id={file_id}"
    last_error = None
    for attempt in range(1, attempts + 1):
        try:
            saved = gdown.download(
                url,
                output=dest_file,
                quiet=False,
                use_cookies=False,
                resume=True,
                user_agent=DRIVE_USER_AGENT,
            )
            if not saved:
                raise RuntimeError(f"gdown returned no file for {url}")
            return saved
        except FileURLRetrievalError as exc:
            last_error = exc
            if attempt >= attempts or not _drive_quota_error(exc):
                raise
            wait = 15 * attempt
            print(
                f"Google Drive refused {os.path.basename(dest_file)} "
                f"(attempt {attempt}/{attempts}). Waiting {wait}s..."
            )
            time.sleep(wait)
    raise last_error


def download_folder(url, dest_dir):
    import gdown
    import inspect

    os.makedirs(dest_dir, exist_ok=True)
    print(f"gdown --folder {url}")
    kwargs = {
        "output": dest_dir,
        "quiet": False,
        "use_cookies": False,
        "skip_download": True,
    }
    if "remaining_ok" in inspect.signature(gdown.download_folder).parameters:
        kwargs["remaining_ok"] = True

    listed = gdown.download_folder(url, **kwargs)
    if not listed:
        raise RuntimeError(f"Could not list Google Drive folder: {url}")
    print(f"Downloading {len(listed)} files")
    for item in listed:
        print(f"gdown {item.path}")
        download_drive_file(item.id, item.local_path)


def download_file(file_id, dest_file):
    url = f"https://drive.google.com/uc?id={file_id}"
    print(f"gdown {url}")
    download_drive_file(file_id, dest_file)


def payload_dir(path):
    if not os.path.isdir(path):
        raise RuntimeError(f"Download produced no files in {path}")
    items = [name for name in os.listdir(path) if name not in SKIP_NAMES]
    if not items:
        raise RuntimeError(f"Download produced no files in {path}")
    if len(items) == 1:
        inner = os.path.join(path, items[0])
        if os.path.isdir(inner):
            if os.path.isdir(os.path.join(inner, "engine")) or os.path.isdir(os.path.join(inner, "game")):
                return inner
            nested_assets = os.path.join(inner, "assets")
            if os.path.isdir(nested_assets):
                return nested_assets
    nested_assets = os.path.join(path, "assets")
    if os.path.isdir(nested_assets) and not os.path.isdir(os.path.join(path, "engine")):
        return nested_assets
    return path


def copy_tree(src, dst):
    os.makedirs(dst, exist_ok=True)
    for dirpath, dirnames, filenames in os.walk(src):
        rel = os.path.relpath(dirpath, src)
        dest_root = dst if rel == "." else os.path.join(dst, rel)
        os.makedirs(dest_root, exist_ok=True)
        for name in dirnames:
            os.makedirs(os.path.join(dest_root, name), exist_ok=True)
        for name in filenames:
            if name in SKIP_NAMES:
                continue
            shutil.copy2(os.path.join(dirpath, name), os.path.join(dest_root, name))


def remove_extras(src, dst):
    kept = set()
    for dirpath, dirnames, filenames in os.walk(src):
        rel = os.path.relpath(dirpath, src)
        prefix = "" if rel == "." else rel
        for name in dirnames:
            kept.add(os.path.normpath(os.path.join(prefix, name)) if prefix else name)
        for name in filenames:
            if name in SKIP_NAMES:
                continue
            kept.add(os.path.normpath(os.path.join(prefix, name)) if prefix else name)

    for dirpath, dirnames, filenames in os.walk(dst, topdown=False):
        rel = os.path.relpath(dirpath, dst)
        prefix = "" if rel == "." else rel
        for name in filenames:
            if name in SKIP_NAMES:
                continue
            key = os.path.normpath(os.path.join(prefix, name)) if prefix else name
            if key not in kept:
                os.remove(os.path.join(dirpath, name))
        for name in dirnames:
            key = os.path.normpath(os.path.join(prefix, name)) if prefix else name
            if key not in kept:
                extra = os.path.join(dirpath, name)
                if extra != dst:
                    shutil.rmtree(extra)


def merge_tree(src, dst, mirror=False):
    copy_tree(src, dst)
    if mirror:
        remove_extras(src, dst)


def rclone_exe():
    path = shutil.which("rclone")
    if not path:
        raise RuntimeError(
            "rclone is required for Push. Install it and run 'rclone config' once on this machine.\n"
            "Config stays local (~/.config/rclone/rclone.conf or %APPDATA%\\rclone\\rclone.conf) and is not committed."
        )
    return path


def read_remote_config(root_dir):
    cfg = {"rcloneRemote": "", "rclonePath": ""}
    path = remote_path(root_dir)
    if os.path.isfile(path):
        raw = read_json(path)
        cfg["rcloneRemote"] = str(raw.get("rcloneRemote") or "")
        cfg["rclonePath"] = str(raw.get("rclonePath") or "")
    env_remote = os.environ.get("IKIGAI_RCLONE_REMOTE", "").strip()
    if env_remote:
        cfg["rcloneRemote"] = env_remote
    return cfg


def resolve_rclone_remote(exe, remote_cfg):
    named = (remote_cfg.get("rcloneRemote") or "").strip().rstrip(":")
    if named:
        return named
    listed = subprocess.run([exe, "listremotes"], check=True, capture_output=True, text=True)
    names = [line.strip().rstrip(":") for line in listed.stdout.splitlines() if line.strip()]
    if len(names) == 1:
        return names[0]
    if not names:
        raise RuntimeError("No rclone remotes. Run 'rclone config' (Google Drive) on this machine.")
    raise RuntimeError(
        "Several rclone remotes: "
        + ", ".join(names)
        + '.\nCreate utils/assets.remote.json (gitignored) with { "rcloneRemote": "NAME" }\n'
        "or set IKIGAI_RCLONE_REMOTE."
    )


def rclone_target(source, remote_cfg, remote_name):
    folder_id = str(source_get(source, "driveFolderId")).strip()
    path = (remote_cfg.get("rclonePath") or "").strip()
    extra = []
    if folder_id and not path:
        extra.extend(["--drive-root-folder-id", folder_id])
        return f"{remote_name}:", extra
    if not path:
        raise ValueError("Set driveFolderId in assets.source.json or rclonePath in assets.remote.json.")
    return f"{remote_name}:{path}", extra


def run_rclone(exe, args):
    print("rclone " + " ".join(args))
    subprocess.run([exe] + args, check=True)


def drive_file_id(exe, spec, extra, file_name):
    result = subprocess.run(
        [exe, "lsjson", spec, "--files-only"] + extra,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or not result.stdout.strip():
        return ""
    try:
        items = json.loads(result.stdout)
    except json.JSONDecodeError:
        return ""
    for item in items:
        if item.get("Name") == file_name:
            return str(item.get("ID") or item.get("Id") or "")
    return ""


def pull(root_dir=None, source=None, mirror=False, dry_run=False):
    root_dir = repo_root(root_dir)
    source = source if source is not None else load_source(root_dir)
    mode = assert_mode(source)
    dest = local_assets_dir(root_dir, source)
    if dry_run:
        if mode == "zip":
            print(f"[dry-run] would download zip {source_get(source, 'driveFileId')} -> {dest} (sha256={source_get(source, 'sha256')})")
        else:
            print(f"[dry-run] would download folder {folder_url(source)} -> {dest}")
        if mirror:
            print(f"[dry-run] pull-mirror would remove extra local files under {dest}")
        return dest

    ensure_gdown()
    work = tempfile.mkdtemp(prefix="IkigaiEngine-assets-")
    try:
        if mode == "zip":
            file_id = str(source_get(source, "driveFileId")).strip()
            if not file_id:
                raise ValueError("mode=zip requires driveFileId. Pack + Push first, or switch mode to folder.")
            zip_file = os.path.join(work, zip_name(source))
            download_file(file_id, zip_file)
            expected = str(source_get(source, "sha256")).lower()
            if expected:
                actual = file_sha256(zip_file)
                if actual != expected:
                    raise RuntimeError(f"sha256 mismatch: expected {expected}, got {actual}")
                print(f"sha256 ok {actual}")
            else:
                print("Warning: assets.source.json has no sha256; skipped integrity check.")
            extract = os.path.join(work, "extract")
            os.makedirs(extract, exist_ok=True)
            with zipfile.ZipFile(zip_file, "r") as archive:
                archive.extractall(extract)
            payload = payload_dir(extract)
        else:
            download_folder(folder_url(source), work)
            payload = payload_dir(work)

        print(("Mirroring" if mirror else "Merging") + f" {payload} -> {dest}")
        merge_tree(payload, dest, mirror=mirror)
        write_stamp(dest, source)
        print(f"Assets ready: {dest}")
        return dest
    finally:
        shutil.rmtree(work, ignore_errors=True)


def pack(root_dir=None, source=None, use_zip_pull=False, dry_run=False):
    root_dir = repo_root(root_dir)
    source = source if source is not None else load_source(root_dir)
    dest = local_assets_dir(root_dir, source)
    if not has_asset_files(dest):
        raise FileNotFoundError(f"Nothing to pack: {dest} is missing or empty. Pull first.")

    version = int(source_get(source, "version", 0) or 0) + 1
    name = zip_name(source)
    zip_file = os.path.join(pack_dir(root_dir), name)
    if dry_run:
        print(f"[dry-run] would zip {dest} -> {zip_file} (version {version})")
        if use_zip_pull:
            print("[dry-run] would set mode=zip")
        return zip_file

    os.makedirs(os.path.dirname(zip_file), exist_ok=True)
    if os.path.isfile(zip_file):
        os.remove(zip_file)
    with zipfile.ZipFile(zip_file, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for file_path in iter_asset_files(dest):
            archive.write(file_path, os.path.relpath(file_path, dest).replace("\\", "/"))

    source["version"] = version
    source["sha256"] = file_sha256(zip_file)
    source["zipFileName"] = name
    if use_zip_pull:
        source["mode"] = "zip"
    save_source(root_dir, source)
    write_stamp(dest, source)
    print(f"Packed {zip_file}")
    print(f"version={version} sha256={source['sha256']}")
    print(f"Updated {source_path(root_dir)} (commit it after Push).")
    return zip_file


def push(root_dir=None, source=None, mirror=False, dry_run=False):
    root_dir = repo_root(root_dir)
    source = source if source is not None else load_source(root_dir)
    mode = assert_mode(source)
    exe = rclone_exe()
    remote_cfg = read_remote_config(root_dir)
    remote_name = resolve_rclone_remote(exe, remote_cfg)
    spec, extra = rclone_target(source, remote_cfg, remote_name)
    verb = "sync" if mirror else "copy"

    args = [verb]
    if mode == "zip":
        zip_file = os.path.join(pack_dir(root_dir), zip_name(source))
        if not os.path.isfile(zip_file):
            raise FileNotFoundError(f"No packed zip at {zip_file}. Run pack first.")
        args.extend([zip_file, spec])
    else:
        local = local_assets_dir(root_dir, source)
        if not has_asset_files(local):
            raise FileNotFoundError(f"Nothing to push: {local} is missing.")
        args.extend([local, spec])
    args.extend(extra)
    if dry_run:
        args.append("--dry-run")
    if mirror and not dry_run:
        print("Warning: push-mirror uses rclone sync and deletes files on Drive that are not in the local source.")
    run_rclone(exe, args)
    if dry_run:
        return

    if mode == "zip":
        file_id = drive_file_id(exe, spec, extra, zip_name(source))
        if file_id:
            source["driveFileId"] = file_id
            save_source(root_dir, source)
            print(f"Wrote driveFileId={file_id} to {source_path(root_dir)}")
        else:
            print("Warning: could not read Drive file id after upload. Set driveFileId in assets.source.json manually.")
    print("Push finished. Commit utils/assets.source.json if Pack/zip metadata changed.")


def ensure_assets(root_dir=None, dry_run=False):
    """Pull when local assets are missing or older than assets.source.json.

    Skip when the local stamp already matches, or when the local tree is a
    newer packed version of the same source (do not clobber unpublished work).
    """
    root_dir = repo_root(root_dir)
    source = load_source(root_dir)
    state, reason = assets_status(root_dir, source)
    dest = local_assets_dir(root_dir, source)

    if state == "current":
        print(f"Assets are up to date ({dest}).")
        return False
    if state == "local-newer":
        print(f"Local assets are newer than assets.source.json ({reason}). Skipping pull.")
        return False
    if state == "unmarked":
        if dry_run:
            print(f"[dry-run] would stamp existing assets as current ({dest})")
            return False
        write_stamp(dest, source)
        print(f"Assets already present; wrote stamp for the current source ({dest}).")
        return False

    print(f"Assets need a pull ({reason}).")
    pull(root_dir, source=source, dry_run=dry_run)
    return not dry_run


def build_parser():
    parser = argparse.ArgumentParser(description="Sync local ./assets with the public Google Drive source.")
    parser.add_argument(
        "action",
        nargs="?",
        default="pull",
        choices=["pull", "pull-mirror", "pack", "push", "push-mirror", "ensure"],
        help="pull is the default. ensure pulls only when the local copy is stale or missing.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print the plan; do not write files.")
    parser.add_argument("--use-zip-pull", action="store_true", help="With pack: set mode to zip.")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    root = repo_root()
    if args.action == "pull":
        pull(root, dry_run=args.dry_run)
    elif args.action == "pull-mirror":
        pull(root, mirror=True, dry_run=args.dry_run)
    elif args.action == "pack":
        pack(root, use_zip_pull=args.use_zip_pull, dry_run=args.dry_run)
    elif args.action == "push":
        push(root, dry_run=args.dry_run)
    elif args.action == "push-mirror":
        push(root, mirror=True, dry_run=args.dry_run)
    else:
        ensure_assets(root, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
