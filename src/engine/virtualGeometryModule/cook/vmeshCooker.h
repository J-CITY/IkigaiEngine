#pragma once

#include <cstdint>
#include <vector>

#include "virtualGeometryModule/cook/vmeshCookSettings.h"
#include "virtualGeometryModule/format/vmeshIO.h"

// Offline virtual mesh cooker (CPU only). The implementation exists only in builds that define IKIGAI_VG_COOKER
// (the Windows build links meshoptimizer); other platforms compile an empty translation unit.
namespace IKIGAI::VG {
struct VMeshCookVertex {
	float Position[3] = { 0.0f, 0.0f, 0.0f };
	float Normal[3] = { 0.0f, 0.0f, 0.0f };
	float Tangent[3] = { 0.0f, 0.0f, 0.0f };
	float Bitangent[3] = { 0.0f, 0.0f, 0.0f };
	float Uv[2] = { 0.0f, 0.0f };
};

// One source mesh in model space; its indices are a triangle list into its own Vertices.
struct VMeshCookMesh {
	uint32_t MaterialSlot = 0;
	std::vector<VMeshCookVertex> Vertices {};
	std::vector<uint32_t> Indices {};
};

struct VMeshCookInput {
	uint32_t MaterialSlotCount = 0; // 0 = derived from the highest MaterialSlot
	uint64_t SourceHash = 0;
	std::vector<VMeshCookMesh> Meshes {};
};

struct VMeshCookReport {
	uint32_t InputVertexCount = 0;
	uint32_t InputTriangleCount = 0;
	uint32_t WeldedVertexCount = 0;
	uint32_t DegenerateTriangleCount = 0; // removed during preparation
	uint32_t CrossMaterialLockedVertexCount = 0;
	uint32_t NonManifoldLockedVertexCount = 0; // pinch vertices and endpoints of edges with more than two triangles
	uint32_t StuckGroupCount = 0; // groups that could not be simplified enough and became roots
	uint32_t DeferredGroupCount = 0; // stuck groups whose clusters were carried to the next level for regrouping
	uint32_t SloppyGroupCount = 0; // groups that needed the sloppy simplifier
	uint32_t RetriedGroupCount = 0; // groups accepted after a gentler retry because the first result broke the topology
	uint32_t TopologyRejectedGroupCount = 0; // groups that became roots because no simplification kept the topology
	double PrepareSeconds = 0.0;
	double ClusterSeconds = 0.0;
	double HierarchySeconds = 0.0;
	double FallbackSeconds = 0.0;
	double TotalSeconds = 0.0;
};

// Builds the full .vmesh content (vertices, level 0 clusters, cluster LOD DAG, fallback mesh) and validates it.
// Throws std::invalid_argument for bad input/settings and VMeshFormatError if the result violates the contract.
void CookVMesh(const VMeshCookInput& input, const VMeshCookSettings& settings, VMeshData& output, VMeshCookReport* report = nullptr);
}
