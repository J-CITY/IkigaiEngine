"""Discover annotated engine headers and generate C++ reflection for CMake."""
import argparse
import os
from pathlib import Path
import sys
from tempfile import NamedTemporaryFile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from utils.build.header_parser import parse
from utils.build.reflection_render import MARKER, render_registration_source, render_registry, render_serde_header


REGISTRY = "reflectionRegistry.generated.cpp"


def discover(root):
    if not root.is_dir():
        raise ValueError(f"source directory does not exist: {root}")
    headers = []
    for directory, children, files in os.walk(root):
        children[:] = sorted(child for child in children if child != "generated")
        for name in sorted(files):
            path = Path(directory, name)
            if path.suffix in {".h", ".hpp"}:
                header = parse(path.read_text(encoding="utf-8"), path)
                if header.classes or header.enums:
                    headers.append(header)
    return headers


def output_plan(headers):
    # Manifest order is public; registration order follows source discovery.
    outputs = {REGISTRY: None}
    for header in sorted(headers, key=lambda item: item.path.stem):
        for suffix in (".generated.h", ".generated.cpp"):
            name = header.path.stem + suffix
            if name in outputs:
                previous = outputs[name]
                origin = previous.path if previous else "the global reflection registry"
                raise ValueError(f"{header.path} and {origin} both produce {name}")
            outputs[name] = header
    return outputs


def write_if_changed(path, content):
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    with NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
        staged = Path(temporary.name)
    try:
        staged.write_text(content, encoding="utf-8")
        staged.replace(path)
    finally:
        staged.unlink(missing_ok=True)


def write_outputs(directory, contents):
    directory.mkdir(parents=True, exist_ok=True)
    for name, content in contents.items():
        write_if_changed(directory / name, content)
    for path in directory.iterdir():
        if path.name in contents or not path.name.endswith((".generated.h", ".generated.cpp")) or not path.is_file():
            continue
        with path.open("rb") as stream:
            marker = stream.readline().strip()
            if marker.startswith(b"#if USE_"):
                marker = stream.readline().strip()
        if marker == MARKER.encode():
            path.unlink()


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("source", type=Path)
    cli.add_argument("output", type=Path)
    cli.add_argument("--list-outputs", action="store_true")
    args = cli.parse_args()
    try:
        headers = discover(args.source)
        outputs = output_plan(headers)
        if args.list_outputs:
            print("\n".join(outputs))
            return
        contents = {}
        for name, header in outputs.items():
            if header is None:
                contents[name] = render_registry(headers)
            elif name.endswith(".h"):
                contents[name] = render_serde_header(header, args.source)
            else:
                contents[name] = render_registration_source(header)
        write_outputs(args.output, contents)
    except (ValueError, OSError) as error:
        cli.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
