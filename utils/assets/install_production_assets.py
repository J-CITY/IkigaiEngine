"""Verify and install the versioned production renderer overlay (stdlib only)."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile


def verified_files(source):
    """Validate the entire delivery before the caller writes any destination file."""
    source = Path(source).resolve()
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != 1 or not manifest.get("bundleVersion"):
        raise ValueError("Unsupported production asset manifest")
    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise ValueError("Production asset manifest has no files")
    result = []
    seen = set()
    for entry in entries:
        relative = entry["path"]
        path = PurePosixPath(relative)
        if (not isinstance(relative, str) or "\\" in relative or ":" in relative
                or path.is_absolute() or ".." in path.parts or len(path.parts) < 2
                or path.parts[0] != "engine" or path.as_posix() != relative):
            raise ValueError(f"Unsafe production asset path: {relative}")
        # Windows destinations are case insensitive, even when verified elsewhere.
        if relative.casefold() in seen:
            raise ValueError(f"Duplicate production asset path: {relative}")
        seen.add(relative.casefold())
        file = source / relative
        if file.is_symlink() or not file.resolve().is_relative_to(source):
            raise ValueError(f"Production asset escapes its source: {relative}")
        data = file.read_bytes()
        if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"Production asset checksum mismatch: {relative}")
        result.append((relative, data))
    actual = {p.relative_to(source).as_posix().casefold()
              for p in (source / "engine").rglob("*") if p.is_file()}
    if actual != seen:
        raise ValueError("Production asset source contains unlisted files")
    return manifest["bundleVersion"], result


def install(source, assets=None):
    version, files = verified_files(source)
    if assets is None:
        return version, len(files), 0
    destination = Path(assets).resolve()
    # Check every target before writing, including symlinked parent directories.
    for relative, _ in files:
        target = destination / relative
        if not target.resolve().is_relative_to(destination) or target.is_symlink():
            raise ValueError(f"Production asset escapes its destination: {relative}")
        if target.exists() and not target.is_file():
            raise ValueError(f"Production asset destination is not a file: {relative}")
        for parent in target.parents:
            if parent == destination:
                break
            if parent.exists() and not parent.is_dir():
                raise ValueError(f"Production asset parent is not a directory: {parent}")
    changed = 0
    for relative, data in files:
        target = destination / relative
        if target.is_file() and target.read_bytes() == data:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as output:
                temporary = Path(output.name)
                output.write(data)
            os.replace(temporary, target)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
        changed += 1
    return version, len(files), changed


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "production_assets")
    parser.add_argument("--assets", type=Path, default=root / "assets")
    parser.add_argument("--verify", action="store_true", help="Check delivery sources without installing")
    args = parser.parse_args()
    try:
        version, count, changed = install(args.source, None if args.verify else args.assets)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Production assets: {error}\n")
    print(f"Production assets {version}: verified {count} files; installed {changed}")


if __name__ == "__main__":
    main()
