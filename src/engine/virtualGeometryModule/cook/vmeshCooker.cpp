#include "vmeshCooker.h"

#if defined(IKIGAI_VG_COOKER)

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

#include <meshoptimizer.h>

#include "virtualGeometryModule/format/vmeshEncoding.h"

namespace IKIGAI::VG {
namespace {
	using Clock = std::chrono::steady_clock;

	constexpr size_t PositionStride = sizeof(float) * 3;
	constexpr size_t AttributeCount = 5; // normal xyz, uv xy
	constexpr size_t AttributeStride = sizeof(float) * AttributeCount;
	constexpr float ClusterSplitFactor = 2.0f;
	constexpr int ClusterOptimizeLevel = 1;

	double SecondsSince(Clock::time_point start) {
		return std::chrono::duration<double>(Clock::now() - start).count();
	}

	// Key for welding: exact bytes are compared by meshopt_generateVertexRemap.
	struct WeldKey {
		float Position[3] = { 0.0f, 0.0f, 0.0f };
		uint32_t Normal = 0;
		float Uv[2] = { 0.0f, 0.0f };
		uint32_t TangentSign = 0;
	};
	static_assert(sizeof(WeldKey) == 28);

	float CanonicalZero(float value) {
		return value == 0.0f ? 0.0f : value; // folds -0 into +0 so both weld
	}

	VMeshFloat3 ToFloat3(const float* value) {
		return VMeshFloat3 { value[0], value[1], value[2] };
	}

	bool IsZero(const float* value) {
		return value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f;
	}

	// Welded, degenerate-free geometry of one material slot. Indices are local to this material.
	struct MaterialGeometry {
		uint32_t Slot = 0;
		uint32_t VertexBase = 0; // first vertex of this material in VMeshData::Vertices
		std::vector<VMeshCookVertex> Vertices {};
		std::vector<float> Positions {};
		std::vector<float> Attributes {};
		std::vector<uint8_t> BaseLocks {}; // meshopt_SimplifyVertex_* flags: cross-material borders, protected seams
		std::vector<uint32_t> Indices {};
	};

	// Cluster in the working set of the hierarchy builder. Indices are local to the material.
	struct WorkCluster {
		size_t IndexOffset = 0; // into the owning index pool
		size_t IndexCount = 0;
		uint32_t GroupId = VMeshLeafGroupId; // group that produced this cluster
		VMeshSphere LodBounds {};
		float Error = 0.0f;
	};

	VMeshSphere ToSphere(const meshopt_Bounds& bounds) {
		VMeshSphere sphere;
		sphere.Center[0] = bounds.center[0];
		sphere.Center[1] = bounds.center[1];
		sphere.Center[2] = bounds.center[2];
		sphere.Radius = bounds.radius;
		return sphere;
	}

	class Cooker {
	public:
		Cooker(const VMeshCookInput& input, const VMeshCookSettings& settings, VMeshData& output, VMeshCookReport& report)
			: m_input(input)
			, m_settings(settings)
			, m_output(output)
			, m_report(report) { }

		void run() {
			const auto start = Clock::now();
			m_output = VMeshData {};
			prepare();
			m_report.PrepareSeconds = SecondsSince(start);
			for (MaterialGeometry& material : m_materials) {
				buildMaterial(material);
			}
			finalizeHeader();
			m_report.TotalSeconds = SecondsSince(start);
			ValidateVMesh(m_output);
		}

	private:
		void validateInput() {
			uint32_t slotCount = m_input.MaterialSlotCount;
			for (size_t meshIndex = 0; meshIndex < m_input.Meshes.size(); ++meshIndex) {
				const VMeshCookMesh& mesh = m_input.Meshes[meshIndex];
				if (mesh.Indices.size() % 3u != 0u) {
					throw std::invalid_argument("Cook input mesh " + std::to_string(meshIndex) + ": index count is not a multiple of 3");
				}
				for (uint32_t index : mesh.Indices) {
					if (index >= mesh.Vertices.size()) {
						throw std::invalid_argument("Cook input mesh " + std::to_string(meshIndex) + ": index out of range");
					}
				}
				for (const VMeshCookVertex& vertex : mesh.Vertices) {
					for (float value : vertex.Position) {
						if (!std::isfinite(value)) {
							throw std::invalid_argument("Cook input mesh " + std::to_string(meshIndex) + ": non-finite vertex position");
						}
					}
				}
				if (m_input.MaterialSlotCount != 0u && mesh.MaterialSlot >= m_input.MaterialSlotCount) {
					throw std::invalid_argument("Cook input mesh " + std::to_string(meshIndex) + ": material slot out of range");
				}
				slotCount = std::max(slotCount, mesh.MaterialSlot + 1u);
			}
			m_slotCount = std::max(slotCount, 1u);
		}

		void prepare() {
			validateInput();
			std::map<uint32_t, std::vector<const VMeshCookMesh*>> meshesBySlot;
			for (const VMeshCookMesh& mesh : m_input.Meshes) {
				m_report.InputVertexCount += uint32_t(mesh.Vertices.size());
				m_report.InputTriangleCount += uint32_t(mesh.Indices.size() / 3u);
				if (!mesh.Indices.empty()) {
					meshesBySlot[mesh.MaterialSlot].push_back(&mesh);
				}
			}
			for (const auto& [slot, meshes] : meshesBySlot) {
				MaterialGeometry material;
				material.Slot = slot;
				weldMaterial(meshes, material);
				removeDegenerates(material);
				if (!material.Indices.empty()) {
					m_materials.push_back(std::move(material));
				}
			}
			if (m_materials.empty()) {
				throw std::invalid_argument("Cook input has no non-degenerate triangles");
			}
			lockCrossMaterialVertices();
			protectSeams();
			emitVertices();
		}

		void weldMaterial(const std::vector<const VMeshCookMesh*>& meshes, MaterialGeometry& material) {
			std::vector<VMeshCookVertex> vertices;
			std::vector<uint32_t> indices;
			for (const VMeshCookMesh* mesh : meshes) {
				const uint32_t base = uint32_t(vertices.size());
				vertices.insert(vertices.end(), mesh->Vertices.begin(), mesh->Vertices.end());
				for (uint32_t index : mesh->Indices) {
					indices.push_back(base + index);
				}
			}
			const float epsilon = m_settings.WeldPositionEpsilon;
			std::vector<WeldKey> keys(vertices.size());
			for (size_t i = 0; i < vertices.size(); ++i) {
				const VMeshCookVertex& vertex = vertices[i];
				WeldKey& key = keys[i];
				for (int axis = 0; axis < 3; ++axis) {
					const float position = epsilon > 0.0f ? std::round(vertex.Position[axis] / epsilon) * epsilon : vertex.Position[axis];
					key.Position[axis] = CanonicalZero(position);
				}
				key.Normal = EncodeNormal(ToFloat3(vertex.Normal));
				key.Uv[0] = CanonicalZero(vertex.Uv[0]);
				key.Uv[1] = CanonicalZero(vertex.Uv[1]);
				key.TangentSign = EncodeTangent(ToFloat3(vertex.Normal), ToFloat3(vertex.Tangent), ToFloat3(vertex.Bitangent)) & VMeshTangentSignBit;
			}
			std::vector<uint32_t> remap(vertices.size());
			const size_t uniqueCount
				= meshopt_generateVertexRemap(remap.data(), indices.data(), indices.size(), keys.data(), keys.size(), sizeof(WeldKey));
			material.Vertices.assign(uniqueCount, VMeshCookVertex {});
			std::vector<uint8_t> written(uniqueCount, 0u);
			for (size_t i = 0; i < vertices.size(); ++i) {
				const uint32_t target = remap[i];
				if (target == ~0u || written[target]) {
					continue; // unreferenced, or a later duplicate (the first occurrence wins)
				}
				written[target] = 1u;
				material.Vertices[target] = vertices[i];
				std::memcpy(material.Vertices[target].Position, keys[i].Position, sizeof(keys[i].Position));
			}
			material.Indices.resize(indices.size());
			meshopt_remapIndexBuffer(material.Indices.data(), indices.data(), indices.size(), remap.data());
			m_report.WeldedVertexCount += uint32_t(uniqueCount);
		}

		void removeDegenerates(MaterialGeometry& material) {
			std::vector<uint32_t> kept;
			kept.reserve(material.Indices.size());
			auto samePosition = [&](uint32_t a, uint32_t b) {
				return std::memcmp(material.Vertices[a].Position, material.Vertices[b].Position, sizeof(float) * 3) == 0;
			};
			for (size_t i = 0; i < material.Indices.size(); i += 3) {
				const uint32_t a = material.Indices[i + 0];
				const uint32_t b = material.Indices[i + 1];
				const uint32_t c = material.Indices[i + 2];
				if (a == b || b == c || a == c || samePosition(a, b) || samePosition(b, c) || samePosition(a, c)) {
					m_report.DegenerateTriangleCount += 1u;
					continue;
				}
				kept.push_back(a);
				kept.push_back(b);
				kept.push_back(c);
			}
			material.Indices.swap(kept);
			material.Positions.resize(material.Vertices.size() * 3u);
			material.Attributes.resize(material.Vertices.size() * AttributeCount);
			for (size_t i = 0; i < material.Vertices.size(); ++i) {
				const VMeshCookVertex& vertex = material.Vertices[i];
				std::memcpy(&material.Positions[i * 3u], vertex.Position, sizeof(float) * 3);
				const VMeshFloat3 normal = DecodeNormal(EncodeNormal(ToFloat3(vertex.Normal)));
				float* attributes = &material.Attributes[i * AttributeCount];
				attributes[0] = normal.X * m_settings.NormalWeight;
				attributes[1] = normal.Y * m_settings.NormalWeight;
				attributes[2] = normal.Z * m_settings.NormalWeight;
				attributes[3] = vertex.Uv[0] * m_settings.UvWeight;
				attributes[4] = vertex.Uv[1] * m_settings.UvWeight;
			}
			material.BaseLocks.assign(material.Vertices.size(), 0u);
		}

		// Vertices whose position is used by more than one material slot never move: every material simplifies
		// independently, so their shared border must stay identical in every LOD and in the fallback mesh.
		void lockCrossMaterialVertices() {
			if (m_materials.size() < 2u) {
				return;
			}
			std::vector<float> positions;
			std::vector<uint32_t> owner;
			for (size_t materialIndex = 0; materialIndex < m_materials.size(); ++materialIndex) {
				positions.insert(positions.end(), m_materials[materialIndex].Positions.begin(), m_materials[materialIndex].Positions.end());
				owner.insert(owner.end(), m_materials[materialIndex].Vertices.size(), uint32_t(materialIndex));
			}
			const size_t count = owner.size();
			std::vector<uint32_t> canonical(count);
			meshopt_generatePositionRemap(canonical.data(), positions.data(), count, PositionStride);
			constexpr uint32_t sharedMarker = ~0u;
			std::vector<uint32_t> firstOwner(count, ~0u - 1u);
			for (size_t i = 0; i < count; ++i) {
				uint32_t& slot = firstOwner[canonical[i]];
				if (slot == ~0u - 1u) {
					slot = owner[i];
				}
				else if (slot != owner[i]) {
					slot = sharedMarker;
				}
			}
			size_t global = 0;
			for (MaterialGeometry& material : m_materials) {
				for (size_t i = 0; i < material.Vertices.size(); ++i, ++global) {
					if (firstOwner[canonical[global]] == sharedMarker) {
						material.BaseLocks[i] |= meshopt_SimplifyVertex_Lock;
						m_report.CrossMaterialLockedVertexCount += 1u;
					}
				}
			}
		}

		// Permissive simplification may collapse across attribute seams; protect vertices whose attributes differ
		// from another vertex at the same position (uv or hard-normal seams).
		void protectSeams() {
			for (MaterialGeometry& material : m_materials) {
				const size_t count = material.Vertices.size();
				std::vector<uint32_t> canonical(count);
				meshopt_generatePositionRemap(canonical.data(), material.Positions.data(), count, PositionStride);
				for (size_t i = 0; i < count; ++i) {
					const uint32_t other = canonical[i];
					if (other == i) {
						continue;
					}
					if (std::memcmp(&material.Attributes[i * AttributeCount], &material.Attributes[other * AttributeCount], AttributeStride) != 0) {
						material.BaseLocks[i] |= meshopt_SimplifyVertex_Protect;
						material.BaseLocks[other] |= meshopt_SimplifyVertex_Protect;
					}
				}
			}
		}

		void emitVertices() {
			bool hasTangents = false;
			for (MaterialGeometry& material : m_materials) {
				material.VertexBase = uint32_t(m_output.Vertices.size());
				for (const VMeshCookVertex& source : material.Vertices) {
					VMeshVertex vertex;
					std::memcpy(vertex.Position, source.Position, sizeof(vertex.Position));
					vertex.Normal = EncodeNormal(ToFloat3(source.Normal));
					vertex.Tangent = EncodeTangent(ToFloat3(source.Normal), ToFloat3(source.Tangent), ToFloat3(source.Bitangent));
					vertex.Uv[0] = source.Uv[0];
					vertex.Uv[1] = source.Uv[1];
					hasTangents = hasTangents || !IsZero(source.Tangent);
					m_output.Vertices.push_back(vertex);
				}
			}
			m_hasTangents = hasTangents;
		}

		size_t minClusterTriangles() const {
			const size_t minimum = size_t(std::floor(float(m_settings.ClusterMaxTriangles) * m_settings.ClusterMinTriangleFraction));
			return std::clamp<size_t>(minimum, 1u, m_settings.ClusterMaxTriangles);
		}

		// Splits a triangle list into clusters appended to clusters/pool (indices local to the material).
		void clusterize(const MaterialGeometry& material, const uint32_t* indices, size_t indexCount, uint32_t groupId,
			std::vector<WorkCluster>& clusters, std::vector<uint32_t>& pool) const {
			const size_t maxVertices = m_settings.ClusterMaxVertices;
			const size_t maxTriangles = m_settings.ClusterMaxTriangles;
			const size_t minTriangles = minClusterTriangles();
			const size_t maxMeshlets = meshopt_buildMeshletsBound(indexCount, maxVertices, minTriangles);
			std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
			std::vector<uint32_t> meshletVertices(indexCount);
			std::vector<uint8_t> meshletTriangles(indexCount);
			const size_t meshletCount = meshopt_buildMeshletsFlex(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices,
				indexCount, material.Positions.data(), material.Vertices.size(), PositionStride, maxVertices, minTriangles, maxTriangles,
				m_settings.ConeWeight, ClusterSplitFactor);
			for (size_t i = 0; i < meshletCount; ++i) {
				const meshopt_Meshlet& meshlet = meshlets[i];
				meshopt_optimizeMeshletLevel(&meshletVertices[meshlet.vertex_offset], meshlet.vertex_count,
					&meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, ClusterOptimizeLevel);
				WorkCluster cluster;
				cluster.IndexOffset = pool.size();
				cluster.IndexCount = size_t(meshlet.triangle_count) * 3u;
				cluster.GroupId = groupId;
				for (size_t j = 0; j < cluster.IndexCount; ++j) {
					pool.push_back(meshletVertices[meshlet.vertex_offset + meshletTriangles[meshlet.triangle_offset + j]]);
				}
				clusters.push_back(cluster);
			}
		}

		meshopt_Bounds tightBounds(const MaterialGeometry& material, const uint32_t* indices, size_t indexCount) const {
			return meshopt_computeClusterBounds(indices, indexCount, material.Positions.data(), material.Vertices.size(), PositionStride);
		}

		// Appends one cluster to the output. parentError/parentBounds describe the group the cluster is merged into.
		void emitCluster(const MaterialGeometry& material, const WorkCluster& cluster, const std::vector<uint32_t>& pool, uint32_t level,
			float parentError, const VMeshSphere& parentBounds) {
			const uint32_t* indices = pool.data() + cluster.IndexOffset;
			const meshopt_Bounds bounds = tightBounds(material, indices, cluster.IndexCount);
			VMeshCluster out;
			out.IndexOffset = uint32_t(m_output.Indices.size());
			out.IndexCount = uint32_t(cluster.IndexCount);
			out.MaterialSlot = material.Slot;
			out.GroupId = level == 0u ? VMeshLeafGroupId : cluster.GroupId;
			out.LodLevel = level;
			out.Error = cluster.Error;
			out.ParentError = parentError;
			out.Bounds = ToSphere(bounds);
			out.LodBounds = level == 0u ? out.Bounds : cluster.LodBounds;
			out.ParentLodBounds = parentError == VMeshRootError ? VMeshSphere {} : parentBounds;
			out.Flags = (level == 0u ? VMESH_CLUSTER_LEAF : 0u) | (parentError == VMeshRootError ? VMESH_CLUSTER_ROOT : 0u);
			if (bounds.cone_cutoff < 1.0f) {
				out.ConeAxis[0] = bounds.cone_axis[0];
				out.ConeAxis[1] = bounds.cone_axis[1];
				out.ConeAxis[2] = bounds.cone_axis[2];
				out.ConeCutoff = bounds.cone_cutoff;
			}
			for (size_t i = 0; i < cluster.IndexCount; ++i) {
				m_output.Indices.push_back(material.VertexBase + indices[i]);
			}
			m_output.Clusters.push_back(out);
			m_maxLevel = std::max(m_maxLevel, level);
		}

		void buildMaterial(MaterialGeometry& material) {
			auto start = Clock::now();
			std::vector<WorkCluster> clusters;
			std::vector<uint32_t> pool;
			clusterize(material, material.Indices.data(), material.Indices.size(), VMeshLeafGroupId, clusters, pool);
			for (WorkCluster& cluster : clusters) {
				cluster.LodBounds = ToSphere(tightBounds(material, pool.data() + cluster.IndexOffset, cluster.IndexCount));
				cluster.Error = 0.0f;
			}
			m_report.ClusterSeconds += SecondsSince(start);
			const size_t firstCluster = m_output.Clusters.size();
			for (const WorkCluster& cluster : clusters) {
				emitCluster(material, cluster, pool, 0u, VMeshRootError, VMeshSphere {});
			}
			start = Clock::now();
			buildSimplifiedFallback(material);
			m_report.FallbackSeconds += SecondsSince(start);
			(void)firstCluster;
		}

		size_t fallbackTargetTriangles(size_t triangleCount) const {
			const size_t ratioTarget = size_t(std::ceil(double(triangleCount) * m_settings.FallbackTriangleRatio));
			return std::min(triangleCount, std::max<size_t>(ratioTarget, m_settings.MinFallbackTriangles));
		}

		void appendFallback(const MaterialGeometry& material, const std::vector<uint32_t>& localIndices) {
			VMeshFallbackRange range;
			range.MaterialSlot = material.Slot;
			range.IndexOffset = uint32_t(m_output.Indices.size());
			range.IndexCount = uint32_t(localIndices.size());
			for (uint32_t index : localIndices) {
				m_output.Indices.push_back(material.VertexBase + index);
			}
			m_output.FallbackRanges.push_back(range);
		}

		// Coarse mesh by direct simplification of the whole material (used without the LOD hierarchy).
		void buildSimplifiedFallback(const MaterialGeometry& material) {
			const size_t triangleCount = material.Indices.size() / 3u;
			const size_t targetIndexCount = fallbackTargetTriangles(triangleCount) * 3u;
			std::vector<uint32_t> result(material.Indices.size());
			const float weights[AttributeCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
			float error = 0.0f;
			size_t count = meshopt_simplifyWithAttributes(result.data(), material.Indices.data(), material.Indices.size(), material.Positions.data(),
				material.Vertices.size(), PositionStride, material.Attributes.data(), AttributeStride, weights, AttributeCount,
				material.BaseLocks.data(), targetIndexCount, FLT_MAX, 0u, &error);
			if (count > targetIndexCount + targetIndexCount / 2u) {
				count = meshopt_simplifySloppy(result.data(), material.Indices.data(), material.Indices.size(), material.Positions.data(),
					material.Vertices.size(), PositionStride, material.BaseLocks.data(), targetIndexCount, FLT_MAX, &error);
			}
			if (count == 0u) {
				count = 3u;
				std::copy(material.Indices.begin(), material.Indices.begin() + 3, result.begin());
			}
			result.resize(count);
			appendFallback(material, result);
		}

		void finalizeHeader() {
			VMeshHeader& header = m_output.Header;
			header = VMeshHeader {};
			header.Flags = m_hasTangents ? VMESH_FLAG_HAS_TANGENTS : VMESH_FLAG_NONE;
			header.SourceHash = m_input.SourceHash;
			header.SettingsHash = HashCookSettings(m_settings);
			header.MaterialSlotCount = m_slotCount;
			header.LodLevelCount = m_maxLevel + 1u;
			const VMeshVertex& first = m_output.Vertices.front();
			for (int axis = 0; axis < 3; ++axis) {
				header.BoundsMin[axis] = first.Position[axis];
				header.BoundsMax[axis] = first.Position[axis];
			}
			for (const VMeshVertex& vertex : m_output.Vertices) {
				for (int axis = 0; axis < 3; ++axis) {
					header.BoundsMin[axis] = std::min(header.BoundsMin[axis], vertex.Position[axis]);
					header.BoundsMax[axis] = std::max(header.BoundsMax[axis], vertex.Position[axis]);
				}
			}
			const meshopt_Bounds sphere
				= meshopt_computeSphereBounds(m_output.Vertices.front().Position, m_output.Vertices.size(), sizeof(VMeshVertex), nullptr, 0);
			header.BoundingSphere = ToSphere(sphere);
			UpdateVMeshSections(m_output);
		}

		const VMeshCookInput& m_input;
		const VMeshCookSettings& m_settings;
		VMeshData& m_output;
		VMeshCookReport& m_report;
		std::vector<MaterialGeometry> m_materials {};
		uint32_t m_slotCount = 1;
		uint32_t m_maxLevel = 0;
		bool m_hasTangents = false;
	};
}

void CookVMesh(const VMeshCookInput& input, const VMeshCookSettings& settings, VMeshData& output, VMeshCookReport* report) {
	ValidateCookSettings(settings);
	VMeshCookReport localReport;
	Cooker cooker(input, settings, output, report ? *report : localReport);
	if (report) {
		*report = VMeshCookReport {};
	}
	cooker.run();
}
}

#endif
