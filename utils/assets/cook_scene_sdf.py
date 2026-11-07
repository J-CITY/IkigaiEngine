"""Bake a static world-space mesh SDF. Requires NumPy.

Usage: python utils/assets/cook_scene_sdf.py scene.json output.texture

The scene JSON specifies Resolution (16–64), Origin (XYZ), Spacing, and
Meshes for a single region, or Resolution and Regions for a BVH of up to
eight regions. Each region supplies Origin, Spacing, and Meshes. Each mesh
specifies Path (a closed, non-self-intersecting triangle
OBJ, relative to the scene file), Albedo (linear RGB in [0, 1]) and
Emissive (linear HDR RGB). All mesh vertices are in world space. Overlapping
solids belong in separate mesh entries so they combine as a signed union.
"""

import argparse
from collections import Counter
from dataclasses import dataclass
import json
import os
from pathlib import Path
import struct

import numpy as np


MAX_MATERIALS = 32
MAX_REGIONS = 8
MAX_ATLAS_BYTES = 2 * 1024 * 1024
MAX_TEXTURE_JSON_BYTES = 8 * 1024 * 1024
MAX_TRIANGLES = 16_384
MAX_SAMPLE_TESTS = 64_000_000
MAX_OBJ_BYTES = 8 * 1024 * 1024
DISTANCE_SCALE = 4096
DISTANCE_BIAS = 32768
MAX_DISTANCE_VOXELS = DISTANCE_BIAS / DISTANCE_SCALE
BVH_NODE_TEXELS = 7
BVH_INVALID_INDEX = 255


@dataclass(frozen=True)
class Mesh:
    path: Path
    albedo: tuple[float, ...]
    emissive: tuple[float, ...]


@dataclass(frozen=True)
class Scene:
    resolution: int
    origin: tuple[float, ...]
    spacing: float
    meshes: tuple[Mesh, ...]


@dataclass(frozen=True)
class MultiRegionScene:
    resolution: int
    regions: tuple[Scene, ...]


def fields(value, expected, label):
    if not isinstance(value, dict) or value.keys() != set(expected):
        raise ValueError(f"{label} must contain exactly {', '.join(expected)}")
    return value


def scalar(value, label, minimum, maximum):
    if type(value) not in (int, float) or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be a number in [{minimum}, {maximum}]")
    # The shader reconstructs the grid from the serialized float32 values.
    return struct.unpack("<f", struct.pack("<f", value))[0]


def triple(value, label, minimum, maximum):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(f"{label} must contain three numbers")
    return tuple(scalar(component, label, minimum, maximum) for component in value)


def load_region(data, resolution, directory):
    origin = triple(data["Origin"], "Origin", -1e6, 1e6)
    spacing = scalar(data["Spacing"], "Spacing", 1e-4, 128)
    if any(abs(component) * np.finfo(np.float32).eps > spacing / 8 for component in origin):
        raise ValueError("Origin is too far from zero for the requested float32 spacing")
    mesh_entries = data["Meshes"]
    if not isinstance(mesh_entries, list) or not 1 <= len(mesh_entries) <= MAX_MATERIALS:
        raise ValueError(f"Meshes must contain 1–{MAX_MATERIALS} entries")
    meshes = []
    for entry in mesh_entries:
        mesh = fields(entry, ("Path", "Albedo", "Emissive"), "Mesh")
        name = mesh["Path"]
        if not isinstance(name, str) or not name or Path(name).is_absolute():
            raise ValueError("Mesh Path must be relative to the scene JSON")
        meshes.append(Mesh(directory / name, triple(mesh["Albedo"], "Albedo", 0, 1),
                           triple(mesh["Emissive"], "Emissive", 0, 65504)))
    return Scene(resolution, origin, spacing, tuple(meshes))


def load_scene(path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    multi = isinstance(data, dict) and "Regions" in data
    expected = ("Resolution", "Regions") if multi else ("Resolution", "Origin", "Spacing", "Meshes")
    fields(data, expected, "Scene")
    resolution = data["Resolution"]
    if type(resolution) is not int or not 16 <= resolution <= 64:
        raise ValueError("Resolution must be an integer in [16, 64]")
    if not multi:
        return load_region(data, resolution, path.parent)

    entries = data["Regions"]
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_REGIONS:
        raise ValueError(f"Regions must contain 1–{MAX_REGIONS} entries")
    width = resolution * resolution
    height = 1 + len(entries) * (resolution + 1)
    if width * height * 4 > MAX_ATLAS_BYTES:
        raise ValueError(f"Region atlas exceeds the {MAX_ATLAS_BYTES} byte budget")
    regions = tuple(load_region(fields(entry, ("Origin", "Spacing", "Meshes"), "Region"), resolution, path.parent)
                    for entry in entries)
    return MultiRegionScene(resolution, regions)


def read_obj(path, triangle_limit):
    if path.stat().st_size > MAX_OBJ_BYTES:
        raise ValueError(f"{path} exceeds the OBJ size budget")
    positions = []
    position_ids = {}
    vertex_ids = []
    faces = []
    edges = Counter()
    with path.open(encoding="utf-8-sig") as stream:
        for line_number, line in enumerate(stream, 1):
            record = line.partition("#")[0].split()
            if not record:
                continue
            tag, *args = record
            if tag == "v":
                if len(args) != 3:
                    raise ValueError(f"{path}:{line_number}: expected an XYZ vertex")
                position = tuple(float(component) for component in args)
                if not all(-1e6 <= component <= 1e6 for component in position):
                    raise ValueError(f"{path}:{line_number}: vertex out of range")
                if position not in position_ids:
                    position_ids[position] = len(positions)
                    positions.append(position)
                vertex_ids.append(position_ids[position])
            elif tag == "f":
                if len(args) != 3:
                    raise ValueError(f"{path}:{line_number}: triangulate faces before baking")
                face = []
                for argument in args:
                    index = int(argument.partition("/")[0])
                    if index == 0 or not -len(vertex_ids) <= index <= len(vertex_ids):
                        raise ValueError(f"{path}:{line_number}: invalid vertex index")
                    face.append(vertex_ids[index - 1 if index > 0 else index])
                if len(set(face)) != 3:
                    raise ValueError(f"{path}:{line_number}: degenerate triangle")
                faces.append(face)
                if len(faces) > triangle_limit:
                    raise ValueError(f"Scene exceeds the {MAX_TRIANGLES} triangle budget")
                for start, end in zip(face, face[1:] + face[:1]):
                    edges[start, end] += 1
    if not faces or any(count != 1 or edges[end, start] != 1 for (start, end), count in edges.items()):
        raise ValueError(f"{path}: expected a closed, consistently wound triangle mesh")
    triangles = np.array(positions, dtype=np.float64)[np.array(faces)]
    normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    if np.any(np.einsum("ij,ij->i", normals, normals) == 0):
        raise ValueError(f"{path}: degenerate triangle")
    return triangles


def coordinate_ranges(axes, minimum, maximum):
    return tuple(slice(axis.searchsorted(low), axis.searchsorted(high, side="right"))
                 for axis, low, high in zip(axes, minimum, maximum))


def point_segment_squared(points, start, end):
    edge = end - start
    delta = points - start
    along = np.clip(np.sum(delta * edge, axis=-1) / np.dot(edge, edge), 0, 1)
    error = delta - along[..., None] * edge
    return np.sum(error * error, axis=-1)


def point_triangle_squared(points, triangle):
    a, b, c = triangle
    ab, ac = b - a, c - a
    normal = np.cross(ab, ac)
    # Edge tests in the triangle plane also work on the unprojected points.
    inside = np.ones(points.shape[:-1], dtype=bool)
    for start, end in ((a, b), (b, c), (c, a)):
        inside &= np.sum((points - start) * np.cross(normal, end - start), axis=-1) >= 0
    height = np.sum((points - a) * normal, axis=-1)
    edge_squared = np.minimum.reduce([point_segment_squared(points, start, end)
                                      for start, end in ((a, b), (b, c), (c, a))])
    return np.where(inside, height * height / np.dot(normal, normal), edge_squared)


def toggle_triangle_crossings(axes, triangle, inside):
    # Cast a +X ray. Use a half-open YZ edge rule to count shared edges once.
    a, b, c = triangle
    ab, ac = b - a, c - a
    normal = np.cross(ab, ac)
    if normal[0] == 0:
        return 0
    if normal[0] < 0:
        b, c = c, b
    sy, sz = coordinate_ranges(axes[1:], triangle.min(axis=0)[1:], triangle.max(axis=0)[1:])
    if sy.start == sy.stop or sz.start == sz.stop:
        return 0
    x, y, z = axes[0][None, None, :], axes[1][sy][None, :], axes[2][sz][:, None]
    covered = np.ones((z.size, y.size), dtype=bool)
    for start, end in ((a, b), (b, c), (c, a)):
        dy, dz = (end - start)[1:]
        edge = dy * (z - start[2]) - dz * (y - start[1])
        inclusive = dz > 0 or (dz == 0 and dy < 0)
        covered &= (edge >= 0) if inclusive else (edge > 0)
    crossing = a[0] - ((y - a[1]) * normal[1] + (z - a[2]) * normal[2]) / normal[0]
    inside[sz, sy, :] ^= covered[..., None] & (x < crossing[..., None])
    return z.size * y.size * x.size


def mesh_distance(triangles, axes, radius, sample_budget):
    shape = (len(axes[0]),) * 3
    squared = np.full(shape, radius * radius)
    inside = np.zeros(shape, dtype=bool)
    for triangle in triangles:
        sx, sy, sz = coordinate_ranges(axes, triangle.min(axis=0) - radius, triangle.max(axis=0) + radius)
        target = squared[sz, sy, sx]
        sample_budget -= target.size + toggle_triangle_crossings(axes, triangle, inside)
        if sample_budget < 0:
            raise ValueError(f"Scene exceeds the {MAX_SAMPLE_TESTS} voxel/triangle test budget")
        if target.size:
            z, y, x = np.meshgrid(axes[2][sz], axes[1][sy], axes[0][sx], indexing="ij")
            points = np.stack((x, y, z), axis=-1)
            np.minimum(target, point_triangle_squared(points, triangle), out=target)
    return np.where(inside, -np.sqrt(squared), np.sqrt(squared)), sample_budget


def bake_region(scene, atlas, triangles_remaining, sample_budget):
    resolution = scene.resolution
    centers = (np.arange(resolution) + 0.5) * scene.spacing
    axes = tuple(origin + centers for origin in scene.origin)
    radius = MAX_DISTANCE_VOXELS * scene.spacing
    shape = (resolution,) * 3
    distances = np.full(shape, np.inf)
    material_ids = np.zeros(shape, dtype=np.uint8)
    for material, mesh in enumerate(scene.meshes):
        triangles = read_obj(mesh.path, triangles_remaining)
        triangles_remaining -= len(triangles)
        signed, sample_budget = mesh_distance(triangles, axes, radius, sample_budget)
        closer = signed < distances
        distances[closer] = signed[closer]
        material_ids[closer] = material

    voxels = atlas[:resolution].reshape(shape + (4,))
    encoded = np.clip(np.rint(distances / scene.spacing * DISTANCE_SCALE) + DISTANCE_BIAS, 0, 65535).astype(np.uint16)
    voxels[..., 0] = encoded & 255
    voxels[..., 1] = encoded >> 8
    voxels[..., 2] = material_ids
    voxels[..., 3] = 255
    metadata = (*scene.origin, scene.spacing, *(value for mesh in scene.meshes for value in (*mesh.albedo, *mesh.emissive)))
    atlas[-1, :len(metadata)] = np.frombuffer(struct.pack(f"<{len(metadata)}f", *metadata), dtype=np.uint8).reshape(-1, 4)
    return triangles_remaining, sample_budget


def region_bounds(scene):
    exact_max = np.array(scene.origin, dtype=np.float64) + scene.spacing * scene.resolution
    rounded_extent = np.float32(scene.spacing * scene.resolution)
    shader_max = np.array(scene.origin, dtype=np.float32) + rounded_extent
    rounded_max = np.maximum(exact_max.astype(np.float32), shader_max)
    # An inward rounding could make the broad phase reject part of a grid.
    upper = np.where(rounded_max < exact_max, np.nextafter(rounded_max, np.float32(np.inf)), rounded_max)
    return np.array(scene.origin, dtype=np.float32), upper


def write_bvh(header, regions):
    header[0] = (ord("S"), ord("D"), ord("F"), len(regions))
    minima, maxima = zip(*(region_bounds(region) for region in regions))
    lower = np.stack(minima)
    upper = np.stack(maxima)
    centroids = (lower.astype(np.float64) + upper) * 0.5

    def write_node(indices, node):
        minimum = lower[indices].min(axis=0)
        maximum = upper[indices].max(axis=0)
        if len(indices) == 1:
            links = (BVH_INVALID_INDEX, BVH_INVALID_INDEX, indices[0], 0)
        else:
            axis = int(np.argmax(np.ptp(centroids[indices], axis=0)))
            ordered = sorted(indices, key=lambda index: (centroids[index, axis], index))
            midpoint = len(ordered) // 2
            left = node + 1
            right = node + 2 * midpoint
            links = (left, right, BVH_INVALID_INDEX, 0)
            write_node(ordered[:midpoint], left)
            write_node(ordered[midpoint:], right)
        offset = 1 + node * BVH_NODE_TEXELS
        header[offset:offset + 6] = np.frombuffer(struct.pack("<6f", *minimum, *maximum), dtype=np.uint8).reshape(6, 4)
        header[offset + 6] = links

    write_node(list(range(len(regions))), 0)


def bake(scene):
    resolution = scene.resolution
    region_height = resolution + 1
    if isinstance(scene, MultiRegionScene):
        regions = scene.regions
        atlas = np.zeros((1 + len(regions) * region_height, resolution * resolution, 4), dtype=np.uint8)
        write_bvh(atlas[0], regions)
        triangles_remaining = MAX_TRIANGLES
        sample_budget = MAX_SAMPLE_TESTS
        for index, region in enumerate(regions):
            first_row = 1 + index * region_height
            triangles_remaining, sample_budget = bake_region(region, atlas[first_row:first_row + region_height],
                                                              triangles_remaining, sample_budget)
    else:
        atlas = np.zeros((region_height, resolution * resolution, 4), dtype=np.uint8)
        bake_region(scene, atlas, MAX_TRIANGLES, MAX_SAMPLE_TESTS)
    return {
        "Width": atlas.shape[1],
        "Height": atlas.shape[0],
        "Channels": 4,
        "ColorData": atlas.reshape(-1).tolist(),
        "TexType": "TEXTURE_2D",
        "PixelType": "RGBA_INT",
        "UseMipmap": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scene", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        texture = bake(load_scene(args.scene))
        encoded = (json.dumps(texture, separators=(",", ":")) + os.linesep).encode("utf-8")
        if len(encoded) > MAX_TEXTURE_JSON_BYTES:
            raise ValueError(f"Texture JSON exceeds the {MAX_TEXTURE_JSON_BYTES} byte budget")
        args.output.write_bytes(encoded)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Scene SDF: {error}\n")


if __name__ == "__main__":
    main()
