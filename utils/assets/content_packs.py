"""Stage the engine, game and platform-selected content packs for a build."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile


PLATFORMS = ("windows", "linux", "macos", "ios", "web", "android", "quest")
STAGE_MARKER = ".ikigai-content-stage"
IGNORED = {".git", ".ikigai-assets-stamp.json", "desktop.ini", ".DS_Store", "Thumbs.db"}


def is_link(path: Path) -> bool:
    """Recognize symlinks and Windows junctions, including broken links."""
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    )


def asset_files(directory: Path):
    if is_link(directory):
        raise ValueError(f"Content directory must not be a link: {directory}")
    if not directory.exists():
        return
    for current, dirs, files in os.walk(directory):
        at = Path(current)
        dirs[:] = sorted(set(dirs) - IGNORED)
        for name in dirs:
            if is_link(at / name):
                raise ValueError(f"Content directory must not be a link: {at / name}")
        for name in sorted(set(files) - IGNORED):
            file = at / name
            if is_link(file) or not file.is_file():
                raise ValueError(f"Expected a regular content file: {file}")
            yield file


def read_packs(root: Path) -> dict:
    packs = {}
    pack_root = root / "assets_packs"
    if is_link(pack_root):
        raise ValueError(f"Content directory must not be a link: {pack_root}")
    if not pack_root.is_dir():
        return packs
    for directory in sorted(pack_root.iterdir()):
        descriptor = directory / "pack.json"
        if not descriptor.is_file():
            continue
        if is_link(directory) or is_link(descriptor):
            raise ValueError(f"Content descriptor must not be a link: {descriptor}")
        data = json.loads(descriptor.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or data.get("id") != directory.name:
            raise ValueError(f"Pack id must match its directory name: {descriptor}")
        platforms = data.get("platforms")
        if not isinstance(platforms, list) or any(name not in (*PLATFORMS, "*") for name in platforms):
            raise ValueError(f"Invalid pack platforms: {descriptor}")
        requires = data.get("requires")
        if not isinstance(requires, list) or any(not isinstance(name, str) or not name for name in requires):
            raise ValueError(f"Invalid pack requirements: {descriptor}")
        packs[directory.name] = data
    return packs


def select_packs(packs: dict, platform: str) -> list[str]:
    selected = set()
    visiting = set()

    def select(pack_id: str):
        if pack_id in selected:
            return
        if pack_id in visiting:
            raise ValueError(f"Cycle in content pack requirements at {pack_id}")
        visiting.add(pack_id)
        for dependency in packs[pack_id]["requires"]:
            if dependency not in packs:
                raise ValueError(f"Content pack {pack_id} requires absent pack {dependency}")
            select(dependency)
        visiting.remove(pack_id)
        selected.add(pack_id)

    for pack_id, data in packs.items():
        if platform in data["platforms"] or "*" in data["platforms"]:
            select(pack_id)
    # Native/Web mounting scans pack directories in alphabetical order.
    return sorted(selected)


def copy_tree(source: Path, destination: Path):
    destination.mkdir(parents=True, exist_ok=True)
    for file in asset_files(source):
        target = destination / file.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def stage(root: Path, destination: Path, platform: str, layout: str):
    root = root.resolve()
    if is_link(destination):
        raise ValueError(f"Stage destination must not be a link: {destination}")
    destination = destination.resolve()
    for source in (root / "assets", root / "assets_packs"):
        source = source.resolve()
        if source.is_relative_to(destination) or destination.is_relative_to(source):
            raise ValueError(f"Stage destination overlaps production assets: {destination}")
    if destination.exists() and (
        not destination.is_dir()
        or (any(destination.iterdir()) and not (destination / STAGE_MARKER).is_file())
    ):
        raise ValueError(f"Stage destination is not an empty or previously staged directory: {destination}")

    pack_ids = select_packs(read_packs(root), platform)
    assets = root / "assets"
    if is_link(assets) or not (assets / "engine").is_dir():
        raise ValueError(f"Expected engine assets in {assets / 'engine'}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix=".ikigai-content-", dir=destination.parent))
    staged = scratch / "staged"
    previous = scratch / "previous"
    published = False
    try:
        asset_output = staged / "assets" if layout == "repo" else staged
        pack_output = staged / ("assets_packs" if layout == "repo" else "packs")
        for subtree in ("engine", "game"):
            copy_tree(assets / subtree, asset_output / subtree)
        pack_output.mkdir(parents=True)
        for pack_id in pack_ids:
            copy_tree(root / "assets_packs" / pack_id, pack_output / pack_id)
        if layout == "apk":
            (staged / "content_packs.json").write_text(json.dumps(pack_ids) + "\n", encoding="utf-8")
        (staged / STAGE_MARKER).touch()

        if destination.exists():
            destination.rename(previous)
        try:
            staged.rename(destination)
            published = True
        except OSError:
            if previous.exists() and not destination.exists():
                previous.rename(destination)
            raise
    finally:
        # A failed rollback must leave the saved stage recoverable.
        if published or not previous.exists():
            shutil.rmtree(scratch)
    print(f"Staged {platform} content in {destination} ({len(pack_ids)} packs)")


def iter_drive_archive(root_dir):
    """Archive Drive-owned bytes, keeping tracked pack descriptors in the repo."""
    root = Path(root_dir)
    game = root / "assets" / "game"
    for file in asset_files(game):
        yield file, (Path("game") / file.relative_to(game)).as_posix()
    for pack_id in read_packs(root):
        if not pack_id.startswith("demo_"):
            continue
        pack = root / "assets_packs" / pack_id
        for file in asset_files(pack):
            if file != pack / "pack.json":
                yield file, file.relative_to(root).as_posix()


# Drive folders created before pack separation store these files under game/.
LEGACY_DEMO_PATHS = {
    "demo_bistro": ("models/bistro", "scenes/rendering/geometry-bistro.json"),
    "demo_sponza": (
        "models/sponza", "models/sponza.obj", "models/sponza.mtl", "scenes/rendering/geometry-sponza.json"
    ),
    "demo_third_person": ("third-person",),
    "demo_pica_pica": ("models/pica_pica", "materials/pica-pica-demo", "shaders/pica-pica-demo", "textures/pica-pica-demo"),
    "demo_meshes": ("models/dancing_vampire.dae", "models/dragon.obj", "models/RunForward.dae", "models/utah_teapot.obj"),
}


def merge_drive_payload(payload, root_dir, mirror=False):
    """Import only game data and known demo payloads from Drive."""
    payload = Path(payload)
    root = Path(root_dir)
    game = payload / "game"
    packs = payload / "assets_packs"
    if payload.name == "assets" and not packs.exists():
        packs = payload.parent / "assets_packs"
    targets = {"game": root / "assets" / "game"}
    targets.update({
        pack_id: root / "assets_packs" / pack_id
        for pack_id in read_packs(root) if pack_id.startswith("demo_")
    })
    incoming = {name: {} for name in targets}
    for file in asset_files(game):
        relative = file.relative_to(game)
        name = next((
            pack_id for pack_id, prefixes in LEGACY_DEMO_PATHS.items() if pack_id in targets
            and any(relative.is_relative_to(prefix) for prefix in prefixes)
        ), "game")
        incoming[name][relative] = file
    for name in targets:
        if name == "game":
            continue
        pack = packs / name
        for file in asset_files(pack):
            if file != pack / "pack.json":
                incoming[name][file.relative_to(pack)] = file
    if not any(incoming.values()):
        raise ValueError(f"Drive payload contains no game or demo content: {payload}")
    for name, files in incoming.items():
        if not files:
            continue
        target = targets[name]
        if is_link(target.parent):
            raise ValueError(f"Asset root must not be a link: {target.parent}")
        existing = list(asset_files(target))
        for relative, source in files.items():
            destination = target / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        if mirror:
            for file in existing:
                relative = file.relative_to(target)
                if relative not in files and not (name != "game" and relative == Path("pack.json")):
                    file.unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    stage_parser = commands.add_parser("stage", help="stage assets in repo or APK layout")
    stage_parser.add_argument("--platform", choices=PLATFORMS, required=True)
    stage_parser.add_argument("--layout", choices=("repo", "apk"), required=True)
    stage_parser.add_argument("--dest", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        stage(Path(__file__).resolve().parents[2], args.dest, args.platform, args.layout)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Cannot stage content: {error}\n")


if __name__ == "__main__":
    main()
