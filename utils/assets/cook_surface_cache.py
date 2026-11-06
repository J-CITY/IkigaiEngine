"""Build an authored surface-cache card asset from world-space OBJ rectangles.

Usage: python utils/assets/cook_surface_cache.py cards.json output.json

Input contains Scene (absolute VFS path) and Cards. Each card contains Path
(OBJ relative to the input JSON), Albedo, Emissive, Roughness, and Thickness.
Albedo and Emissive are linear RGB objects with X, Y, and Z fields. Each OBJ
contains two consistently wound triangles forming one opaque rectangle.
"""

import argparse
import json
import math
from pathlib import Path


MAX_CARDS = 16
MAX_SOURCE_BYTES = 1024 * 1024
MAX_WORLD_COORDINATE = 10_000
MIN_AXIS_LENGTH = 0.01
MAX_AXIS_LENGTH = 128
RECTANGLE_TOLERANCE = 1e-5


def fields(value, expected, label):
    if not isinstance(value, dict) or value.keys() != set(expected):
        raise ValueError(f"{label} must contain exactly {', '.join(expected)}")
    return value


def scalar(value, label, minimum, maximum):
    if type(value) not in (int, float) or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be a number in [{minimum}, {maximum}]")
    return value


def rgb(value, label, maximum):
    return {axis: scalar(component, label, 0, maximum)
            for axis, component in fields(value, "XYZ", label).items()}


def vector(value):
    return dict(zip("XYZ", value))


def subtract(left, right):
    return tuple(a - b for a, b in zip(left, right))


def read_rectangle(path):
    if path.stat().st_size > MAX_SOURCE_BYTES:
        raise ValueError(f"{path}: OBJ exceeds the source size limit")

    vertices = []
    faces = []
    with path.open(encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            record = line.partition("#")[0].split()
            if not record:
                continue
            tag, *args = record
            if tag == "v":
                if len(args) != 3:
                    raise ValueError(f"{path}:{line_number}: expected an XYZ vertex")
                vertices.append(tuple(scalar(float(component), "Vertex", -MAX_WORLD_COORDINATE,
                                             MAX_WORLD_COORDINATE) for component in args))
            elif tag == "f":
                if len(args) != 3 or len(faces) >= 2:
                    raise ValueError(f"{path}:{line_number}: expected exactly two triangles")
                face = []
                for argument in args:
                    index = int(argument.partition("/")[0])
                    if index == 0 or not -len(vertices) <= index <= len(vertices):
                        raise ValueError(f"{path}:{line_number}: invalid vertex index")
                    face.append(vertices[index - 1 if index > 0 else index])
                if len(set(face)) != 3:
                    raise ValueError(f"{path}:{line_number}: degenerate triangle")
                faces.append(face)
    if len(faces) != 2:
        raise ValueError(f"{path}: expected exactly two triangles")

    first, second = faces
    shared = set(first) & set(second)
    if len(shared) != 2:
        raise ValueError(f"{path}: triangles must share the rectangle diagonal")
    if set(vertices) != set(first) | set(second):
        raise ValueError(f"{path}: vertices outside the four used corners")

    origin = next(vertex for vertex in first if vertex not in shared)
    corner = first.index(origin)
    end_u, end_v = first[(corner + 1) % 3], first[(corner + 2) % 3]
    if second[(second.index(end_v) + 1) % 3] != end_u:
        raise ValueError(f"{path}: inconsistent triangle winding")

    axis_u, axis_v = subtract(end_u, origin), subtract(end_v, origin)
    length_u, length_v = math.hypot(*axis_u), math.hypot(*axis_v)
    if min(length_u, length_v) < MIN_AXIS_LENGTH or max(length_u, length_v) > MAX_AXIS_LENGTH:
        raise ValueError(f"{path}: card axes must be in [{MIN_AXIS_LENGTH}, {MAX_AXIS_LENGTH}] world units")
    dot = math.fsum(a * b for a, b in zip(axis_u, axis_v))
    if abs(dot) > RECTANGLE_TOLERANCE * length_u * length_v:
        raise ValueError(f"{path}: card axes must be perpendicular")

    opposite = next(vertex for vertex in second if vertex not in shared)
    delta = tuple(end - start - u - v for end, start, u, v in zip(opposite, origin, axis_u, axis_v))
    if math.hypot(*delta) > RECTANGLE_TOLERANCE * min(length_u, length_v):
        raise ValueError(f"{path}: fourth corner does not close the rectangle")
    return {"Origin": vector(origin), "AxisU": vector(axis_u), "AxisV": vector(axis_v)}


def cook(path):
    if path.stat().st_size > MAX_SOURCE_BYTES:
        raise ValueError("Card description exceeds the source size limit")
    spec = fields(json.loads(path.read_text(encoding="utf-8-sig")), ("Scene", "Cards"), "Description")
    scene, entries = spec["Scene"], spec["Cards"]
    if (not isinstance(scene, str) or not scene.startswith("/") or "\\" in scene
            or any(part in ("", ".", "..") for part in scene[1:].split("/"))):
        raise ValueError("Scene must be an absolute VFS path")
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_CARDS:
        raise ValueError(f"Cards must contain 1–{MAX_CARDS} entries")

    cards = []
    for entry in entries:
        fields(entry, ("Path", "Albedo", "Emissive", "Roughness", "Thickness"), "Card")
        mesh = entry["Path"]
        if not isinstance(mesh, str) or not mesh or Path(mesh).anchor:
            raise ValueError("Card Path must be relative to the description JSON")
        cards.append({
            **read_rectangle(path.parent / mesh),
            "Albedo": rgb(entry["Albedo"], "Albedo", 1),
            "Emissive": rgb(entry["Emissive"], "Emissive", 65504),
            "Roughness": scalar(entry["Roughness"], "Roughness", 0.7, 1),
            "Thickness": scalar(entry["Thickness"], "Thickness", 0.0001, 0.02),
        })
    return {"Scene": scene, "Cards": cards}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("description", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = cook(args.description)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Surface cache: {error}\n")


if __name__ == "__main__":
    main()
