#pragma once

#include <cstddef>
#include <cstdint>

// On-disk and in-memory layout of a cooked virtual mesh (.vmesh), version 1.
//
// Shared contract between the offline cooker (virtualGeometryModule/cook) and the runtime
// (virtualGeometryModule/runtime). Plain data only; every struct is little-endian, 4-byte aligned
// and has no hidden padding (see the static_asserts). GPU code reads VMeshVertex and VMeshCluster
// directly from storage buffers, so a layout change must bump VMeshVersion.
//
// File layout: VMeshHeader, then the sections named in the header, each aligned to 16 bytes.
//   Vertices        VMeshVertex[VertexCount]        shared by every LOD level and the fallback mesh
//   Indices         uint32_t[IndexCount]            triangle lists; values index the Vertices section
//   Clusters        VMeshCluster[ClusterCount]      every LOD level of the cluster DAG
//   FallbackRanges  VMeshFallbackRange[Count]       coarse mesh for backends/passes without cluster rendering
//
// Cluster LOD DAG (error-bounded, crack-free, see docs/rendering/m2-virtual-geometry-spec.md):
//   Level 0 clusters hold the original triangles. Higher levels are built by merging and simplifying
//   groups of clusters; simplified clusters only reference original vertices, so the Vertices section
//   is never duplicated. All clusters of one group share Error, LodBounds, ParentError and
//   ParentLodBounds. A cluster is drawn when
//       ProjectedError(Error, LodBounds) <= Threshold < ProjectedError(ParentError, ParentLodBounds)
//   where ProjectedError(e, s) is the screen-space size in pixels of an object-space error e at the
//   closest point of sphere s. Roots have ParentError = VMeshRootError.
namespace IKIGAI::VG {
inline constexpr uint32_t VMeshMagic = 0x48534D56u; // "VMSH" read as little-endian bytes
inline constexpr uint32_t VMeshVersion = 1u;
inline constexpr uint32_t VMeshVertexLayoutVersion = 1u;
inline constexpr uint32_t VMeshMaxClusterTriangles = 128u;
inline constexpr float VMeshRootError = 3.402823466e+38f; // FLT_MAX: a root cluster is never replaced by a coarser one

enum VMeshHeaderFlags : uint32_t {
	VMESH_FLAG_NONE = 0u,
	VMESH_FLAG_HAS_TANGENTS = 1u << 0,
};

enum VMeshClusterFlags : uint32_t {
	VMESH_CLUSTER_LEAF = 1u << 0, // LodLevel 0: original, unsimplified triangles
	VMESH_CLUSTER_ROOT = 1u << 1, // no parent: ParentError == VMeshRootError
};

struct VMeshSphere {
	float Center[3] = { 0.0f, 0.0f, 0.0f };
	float Radius = 0.0f;
};
static_assert(sizeof(VMeshSphere) == 16);

// Position is in model space (node transforms are not applied, matching the legacy import path).
// Normal: octahedral encoding of the unit normal, two snorm16 (x in the low 16 bits, y in the high 16 bits).
// Tangent: octahedral encoding of the unit tangent, two snorm15 (x in bits 0..14, y in bits 15..29, each
//          stored as two's complement in 15 bits); bit 31 is 1 when the bitangent sign is negative
//          (bitangent = sign * cross(normal, tangent)); bit 30 is reserved and written as 0.
// The exact encode/decode formulas are in docs/rendering/m2-virtual-geometry-spec.md (section "Vertex encoding").
struct VMeshVertex {
	float Position[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t Normal = 0;
	uint32_t Tangent = 0;
	float Uv[2] = { 0.0f, 0.0f };
};
static_assert(sizeof(VMeshVertex) == 28);

struct VMeshCluster {
	uint32_t IndexOffset = 0; // first index in the Indices section
	uint32_t IndexCount = 0;  // multiple of 3, at most 3 * VMeshMaxClusterTriangles
	uint32_t MaterialSlot = 0; // index into the model material list (importer material order); a cluster never mixes materials
	uint32_t GroupId = 0;      // simplification group that produced this cluster (0xFFFFFFFF for level 0)
	uint32_t LodLevel = 0;     // 0 = original geometry, increasing = coarser
	uint32_t Flags = 0;        // VMeshClusterFlags
	float Error = 0.0f;        // object-space error of this cluster against the original mesh (0 for level 0)
	float ParentError = VMeshRootError;
	VMeshSphere Bounds;          // tight bounds of the cluster triangles (culling)
	VMeshSphere LodBounds;       // sphere used to project Error; identical for all clusters of a group
	VMeshSphere ParentLodBounds; // sphere used to project ParentError; zero radius for roots
	float ConeAxis[3] = { 0.0f, 0.0f, 0.0f }; // normal cone for backface culling (unit axis, or zero when disabled)
	float ConeCutoff = 1.0f;                   // cos(half angle) + margin; >= 1 means "never cull"
};
static_assert(sizeof(VMeshCluster) == 96);

struct VMeshFallbackRange {
	uint32_t MaterialSlot = 0;
	uint32_t IndexOffset = 0;
	uint32_t IndexCount = 0;
	uint32_t Reserved = 0;
};
static_assert(sizeof(VMeshFallbackRange) == 16);

struct VMeshSection {
	uint64_t Offset = 0; // from the start of the file
	uint64_t Size = 0;   // bytes
	uint32_t Count = 0;  // elements
	uint32_t Stride = 0; // bytes per element
};
static_assert(sizeof(VMeshSection) == 24);

struct VMeshHeader {
	uint32_t Magic = VMeshMagic;
	uint32_t Version = VMeshVersion;
	uint32_t VertexLayoutVersion = VMeshVertexLayoutVersion;
	uint32_t Flags = VMESH_FLAG_NONE; // VMeshHeaderFlags
	uint64_t SourceHash = 0;          // hash of the source model bytes (and sidecars the importer read)
	uint64_t SettingsHash = 0;        // hash of the serialized cook settings
	uint32_t MaterialSlotCount = 0;
	uint32_t LodLevelCount = 0;
	float BoundsMin[3] = { 0.0f, 0.0f, 0.0f };
	float BoundsMax[3] = { 0.0f, 0.0f, 0.0f };
	VMeshSphere BoundingSphere;
	VMeshSection Vertices;
	VMeshSection Indices;
	VMeshSection Clusters;
	VMeshSection FallbackRanges;
	uint32_t Reserved[8] = {};
};
static_assert(sizeof(VMeshHeader) == 208);
}
