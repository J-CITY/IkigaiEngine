#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>

namespace IKIGAI::VG {
// Cooker settings (serdepp). The serialized JSON of these settings is hashed into VMeshHeader::SettingsHash.
struct VMeshCookSettings {
	// Level 0 and simplified clusters: at most this many triangles (<= VMeshMaxClusterTriangles) and vertices.
	uint32_t ClusterMaxTriangles = 128;
	uint32_t ClusterMaxVertices = 128;
	// Clusterizer may stop a cluster below the maximum, but not below this fraction of ClusterMaxTriangles.
	float ClusterMinTriangleFraction = 1.0f / 3.0f;
	// Weight of normal cone compactness when building clusters (0 = spatial only); helps cluster backface culling.
	float ConeWeight = 0.25f;
	// Target number of clusters merged into one simplification group (meshopt_partitionClusters target; actual
	// groups may be up to a third larger).
	uint32_t GroupSize = 8;
	// Every DAG level aims to keep this fraction of the triangles of a group.
	float SimplifyRatio = 0.5f;
	// A group whose simplified triangle count stays above this fraction of its input is "stuck" and becomes a root.
	float SimplifyStuckThreshold = 0.85f;
	// Attribute weights of the attribute-aware simplifier (normal xyz, uv xy).
	float NormalWeight = 0.5f;
	float UvWeight = 0.25f;
	// Error multiplier for groups that needed the sloppy (topology-ignoring) simplifier.
	float SloppyErrorFactor = 2.0f;
	// Fallback mesh: fraction of the level 0 triangles per material slot, but at least MinFallbackTriangles.
	float FallbackTriangleRatio = 0.02f;
	uint32_t MinFallbackTriangles = 64;
	// Position snapping grid used when welding vertices (0 = exact positions). Welding also requires equal encoded
	// normals, uvs and bitangent signs, inside one material slot only.
	float WeldPositionEpsilon = 0.0f;
	// false: only level 0 clusters (every cluster is a root) and a simplified fallback mesh.
	bool BuildLodHierarchy = true;
	// Reject simplified groups that pinch edges (more than two triangles), open holes or add chords between locked
	// border vertices; the offending vertices are locked and the group is simplified again. Keeps every DAG cut of a
	// closed model watertight and edge-manifold, at the cost of more root clusters around thin features.
	bool PreserveTopology = true;
	// Target size of an independently readable geometry page.
	uint32_t PageBytes = 64u * 1024u;

	template <class Context> constexpr static auto serde(Context& context, VMeshCookSettings& value) {
		using Self = VMeshCookSettings;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::ClusterMaxTriangles, "ClusterMaxTriangles", default_ { 128u })
			.field(&Self::ClusterMaxVertices, "ClusterMaxVertices", default_ { 128u })
			.field(&Self::ClusterMinTriangleFraction, "ClusterMinTriangleFraction", default_ { 1.0f / 3.0f })
			.field(&Self::ConeWeight, "ConeWeight", default_ { 0.25f })
			.field(&Self::GroupSize, "GroupSize", default_ { 8u })
			.field(&Self::SimplifyRatio, "SimplifyRatio", default_ { 0.5f })
			.field(&Self::SimplifyStuckThreshold, "SimplifyStuckThreshold", default_ { 0.85f })
			.field(&Self::NormalWeight, "NormalWeight", default_ { 0.5f })
			.field(&Self::UvWeight, "UvWeight", default_ { 0.25f })
			.field(&Self::SloppyErrorFactor, "SloppyErrorFactor", default_ { 2.0f })
			.field(&Self::FallbackTriangleRatio, "FallbackTriangleRatio", default_ { 0.02f })
			.field(&Self::MinFallbackTriangles, "MinFallbackTriangles", default_ { 64u })
			.field(&Self::WeldPositionEpsilon, "WeldPositionEpsilon", default_ { 0.0f })
			.field(&Self::BuildLodHierarchy, "BuildLodHierarchy", default_ { true })
			.field(&Self::PreserveTopology, "PreserveTopology", default_ { true })
			.field(&Self::PageBytes, "PageBytes", default_ { 64u * 1024u });
	}
};

// Throws std::invalid_argument when a setting is out of range (checked when the settings are loaded/used).
void ValidateCookSettings(const VMeshCookSettings& settings);
// Canonical JSON of the settings and its hash (VMeshHeader::SettingsHash).
std::string SerializeCookSettings(const VMeshCookSettings& settings);
uint64_t HashCookSettings(const VMeshCookSettings& settings);
// Parses JSON (missing fields keep their defaults) and validates. Throws on malformed input.
VMeshCookSettings ParseCookSettings(const std::string& json);
}
