#include "virtualGeometryModule/cook/vmeshCooker.h"

#if defined(IKIGAI_VG_COOKER)

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

#include <meshoptimizer.h>

#include "virtualGeometryModule/cook/vmeshCookTopology.h"
#include "virtualGeometryModule/format/vmeshEncoding.h"

namespace IKIGAI::VG {
namespace {
	using Clock = std::chrono::steady_clock;
	using COOK::CheckTopology;
	using COOK::EdgeKey;
	using COOK::RemoveFinPairs;

	constexpr size_t PositionStride = sizeof(float) * 3;
	constexpr size_t AttributeCount = 5; // normal xyz, uv xy
	constexpr size_t AttributeStride = sizeof(float) * AttributeCount;
	constexpr float ClusterSplitFactor = 2.0f;
	constexpr int ClusterOptimizeLevel = 1;
	constexpr uint32_t MaxStuckDeferrals = 2;

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
		uint32_t Generation = 0; // LodLevel: 0 for original triangles, else 1 + highest member generation of the producing group
		uint32_t Deferrals = 0; // times this cluster was carried to the next level because its group was stuck
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
			lockNonManifoldVertices();
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

		// Vertices where the surface is pinched (several separate triangle fans meet at one position) or where an
		// edge is shared by more than two triangles are locked: collapsing them joins unrelated sheets and creates
		// non-manifold edges in simplified levels.
		void lockNonManifoldVertices() {
			for (MaterialGeometry& material : m_materials) {
				const size_t count = material.Vertices.size();
				std::vector<uint32_t> canonical(count);
				meshopt_generatePositionRemap(canonical.data(), material.Positions.data(), count, PositionStride);
				struct Corner {
					uint32_t Vertex = 0;
					uint32_t Next = 0;
					uint32_t Previous = 0;
				};
				std::vector<Corner> corners;
				corners.reserve(material.Indices.size());
				std::vector<uint64_t> edges;
				edges.reserve(material.Indices.size());
				for (size_t i = 0; i < material.Indices.size(); i += 3) {
					const uint32_t v[3] = { canonical[material.Indices[i]], canonical[material.Indices[i + 1]], canonical[material.Indices[i + 2]] };
					for (int k = 0; k < 3; ++k) {
						corners.push_back(Corner { v[k], v[(k + 1) % 3], v[(k + 2) % 3] });
						edges.push_back(EdgeKey(v[k], v[(k + 1) % 3]));
					}
				}
				std::vector<uint8_t> pinched(count, 0u);
				std::sort(edges.begin(), edges.end());
				for (size_t i = 0; i + 2 < edges.size(); ++i) {
					if (edges[i] == edges[i + 2]) {
						pinched[uint32_t(edges[i] >> 32)] = 1u;
						pinched[uint32_t(edges[i] & 0xFFFFFFFFu)] = 1u;
					}
				}
				std::sort(corners.begin(), corners.end(), [](const Corner& a, const Corner& b) { return a.Vertex < b.Vertex; });
				std::vector<uint32_t> linkVertices;
				std::vector<uint32_t> parent;
				for (size_t i = 0; i < corners.size();) {
					size_t j = i;
					while (j < corners.size() && corners[j].Vertex == corners[i].Vertex) {
						++j;
					}
					// Connected components of the link (the ring of opposite edges) of this vertex.
					linkVertices.clear();
					for (size_t k = i; k < j; ++k) {
						linkVertices.push_back(corners[k].Next);
						linkVertices.push_back(corners[k].Previous);
					}
					std::sort(linkVertices.begin(), linkVertices.end());
					linkVertices.erase(std::unique(linkVertices.begin(), linkVertices.end()), linkVertices.end());
					parent.resize(linkVertices.size());
					for (size_t k = 0; k < parent.size(); ++k) {
						parent[k] = uint32_t(k);
					}
					auto find = [&](uint32_t x) {
						while (parent[x] != x) {
							parent[x] = parent[parent[x]];
							x = parent[x];
						}
						return x;
					};
					auto indexOf = [&](uint32_t vertex) {
						return uint32_t(std::lower_bound(linkVertices.begin(), linkVertices.end(), vertex) - linkVertices.begin());
					};
					size_t components = linkVertices.size();
					for (size_t k = i; k < j; ++k) {
						const uint32_t a = find(indexOf(corners[k].Next));
						const uint32_t b = find(indexOf(corners[k].Previous));
						if (a != b) {
							parent[a] = b;
							components -= 1u;
						}
					}
					if (components > 1u) {
						pinched[corners[i].Vertex] = 1u;
					}
					i = j;
				}
				for (size_t i = 0; i < count; ++i) {
					if (pinched[canonical[i]]) {
						material.BaseLocks[i] |= meshopt_SimplifyVertex_Lock;
						m_report.NonManifoldLockedVertexCount += 1u;
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
		void emitCluster(const MaterialGeometry& material, const WorkCluster& cluster, const std::vector<uint32_t>& pool, float parentError,
			const VMeshSphere& parentBounds) {
			const uint32_t level = cluster.Generation;
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
			if (!m_settings.BuildLodHierarchy) {
				for (const WorkCluster& cluster : clusters) {
					emitCluster(material, cluster, pool, VMeshRootError, VMeshSphere {});
				}
				start = Clock::now();
				buildSimplifiedFallback(material);
				m_report.FallbackSeconds += SecondsSince(start);
				return;
			}
			start = Clock::now();
			buildHierarchy(material, clusters, pool);
			m_report.HierarchySeconds += SecondsSince(start);
			start = Clock::now();
			buildCutFallback(material, firstCluster);
			m_report.FallbackSeconds += SecondsSince(start);
		}

		// Sphere around the member spheres, grown so that it provably contains every member (required for a
		// monotonic projected error between a group and its parent).
		static VMeshSphere MergeSpheres(const std::vector<VMeshSphere>& spheres) {
			const meshopt_Bounds merged
				= meshopt_computeSphereBounds(&spheres[0].Center[0], spheres.size(), sizeof(VMeshSphere), &spheres[0].Radius, sizeof(VMeshSphere));
			VMeshSphere result = ToSphere(merged);
			double radius = result.Radius;
			for (const VMeshSphere& sphere : spheres) {
				const double dx = double(sphere.Center[0]) - result.Center[0];
				const double dy = double(sphere.Center[1]) - result.Center[1];
				const double dz = double(sphere.Center[2]) - result.Center[2];
				radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz) + sphere.Radius);
			}
			result.Radius = std::nextafter(float(radius), FLT_MAX);
			return result;
		}

		// Partitions clusters into groups of about GroupSize adjacent clusters. Reorders clusters so that every group
		// is contiguous and returns the group offsets (group count + 1 entries).
		std::vector<uint32_t> partition(const MaterialGeometry& material, std::vector<WorkCluster>& clusters, const std::vector<uint32_t>& pool,
			const std::vector<uint32_t>& remap) const {
			if (clusters.size() <= m_settings.GroupSize) {
				return { 0u, uint32_t(clusters.size()) };
			}
			std::vector<uint32_t> counts(clusters.size());
			std::vector<uint32_t> remapped;
			for (size_t i = 0; i < clusters.size(); ++i) {
				counts[i] = uint32_t(clusters[i].IndexCount);
				for (size_t j = 0; j < clusters[i].IndexCount; ++j) {
					remapped.push_back(remap[pool[clusters[i].IndexOffset + j]]);
				}
			}
			std::vector<uint32_t> part(clusters.size());
			const size_t partCount = meshopt_partitionClusters(part.data(), remapped.data(), remapped.size(), counts.data(), counts.size(),
				material.Positions.data(), material.Vertices.size(), PositionStride, m_settings.GroupSize);
			std::vector<uint32_t> offsets(partCount + 1u, 0u);
			for (uint32_t p : part) {
				offsets[p + 1u] += 1u;
			}
			for (size_t i = 1; i <= partCount; ++i) {
				offsets[i] += offsets[i - 1u];
			}
			std::vector<uint32_t> write(offsets.begin(), offsets.end() - 1);
			std::vector<WorkCluster> sorted(clusters.size());
			for (size_t i = 0; i < clusters.size(); ++i) {
				sorted[write[part[i]]++] = clusters[i];
			}
			clusters.swap(sorted);
			return offsets;
		}

		// Locks every vertex (by position) used by more than one group, so the outer border of each group stays fixed
		// while it is simplified; adds the material base locks (cross-material borders, protected seams).
		void lockGroupBorders(const MaterialGeometry& material, const std::vector<WorkCluster>& clusters, const std::vector<uint32_t>& offsets,
			const std::vector<uint32_t>& pool, const std::vector<uint32_t>& remap, const std::vector<uint8_t>& frozen,
			std::vector<uint8_t>& locks) const {
			const size_t count = material.Vertices.size();
			std::vector<uint32_t> lastGroup(count, ~0u);
			std::vector<uint8_t> shared(count, 0u);
			for (size_t group = 0; group + 1u < offsets.size(); ++group) {
				for (uint32_t c = offsets[group]; c < offsets[group + 1u]; ++c) {
					const WorkCluster& cluster = clusters[c];
					for (size_t j = 0; j < cluster.IndexCount; ++j) {
						const uint32_t r = remap[pool[cluster.IndexOffset + j]];
						if (lastGroup[r] != ~0u && lastGroup[r] != uint32_t(group)) {
							shared[r] = 1u;
						}
						lastGroup[r] = uint32_t(group);
					}
				}
			}
			locks.resize(count);
			for (size_t i = 0; i < count; ++i) {
				locks[i] = uint8_t(((shared[remap[i]] || frozen[remap[i]]) ? meshopt_SimplifyVertex_Lock : 0) | material.BaseLocks[i]);
			}
		}

		// Simplifies a merged group (indices local to the material) towards targetIndexCount and returns the absolute
		// object-space error of the result.
		float simplifyGroup(const MaterialGeometry& material, const std::vector<uint32_t>& merged, const std::vector<uint8_t>& locks,
			size_t targetIndexCount, std::vector<uint32_t>& result) {
			const float weights[AttributeCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
			const unsigned int options
				= meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute | meshopt_SimplifyErrorClamped | meshopt_SimplifyPermissive;
			result.resize(merged.size());
			float error = 0.0f;
			result.resize(meshopt_simplifyWithAttributes(result.data(), merged.data(), merged.size(), material.Positions.data(),
				material.Vertices.size(), PositionStride, material.Attributes.data(), AttributeStride, weights, AttributeCount, locks.data(),
				targetIndexCount, FLT_MAX, options, &error));
			if (double(result.size()) > double(merged.size()) * m_settings.SimplifyStuckThreshold) {
				// Topology-preserving simplification is stuck: sloppy simplification of the group subset. Locked border
				// vertices are kept, so the group border stays crack-free.
				struct SloppyVertex {
					float Position[3] = { 0.0f, 0.0f, 0.0f };
					uint32_t Id = 0;
				};
				std::vector<SloppyVertex> subset(merged.size());
				std::vector<uint8_t> subsetLocks(merged.size());
				std::vector<uint32_t> identity(merged.size());
				for (size_t i = 0; i < merged.size(); ++i) {
					std::memcpy(subset[i].Position, &material.Positions[size_t(merged[i]) * 3u], sizeof(float) * 3);
					subset[i].Id = merged[i];
					subsetLocks[i] = locks[merged[i]] & meshopt_SimplifyVertex_Lock;
					identity[i] = uint32_t(i);
				}
				std::vector<uint32_t> sloppy(merged.size());
				float sloppyError = 0.0f;
				sloppy.resize(meshopt_simplifySloppy(sloppy.data(), identity.data(), identity.size(), subset[0].Position, subset.size(),
					sizeof(SloppyVertex), subsetLocks.data(), targetIndexCount, FLT_MAX, &sloppyError));
				if (!sloppy.empty() && sloppy.size() < result.size()) {
					sloppyError *= meshopt_simplifyScale(subset[0].Position, subset.size(), sizeof(SloppyVertex));
					result.resize(sloppy.size());
					for (size_t i = 0; i < sloppy.size(); ++i) {
						result[i] = subset[sloppy[i]].Id;
					}
					error = std::max(error, sloppyError * m_settings.SloppyErrorFactor);
					m_report.SloppyGroupCount += 1u;
				}
			}
			if (result.empty()) {
				result.assign(merged.begin(), merged.begin() + 3); // never produce an empty cluster
			}
			return error;
		}

		// Simplifies a group towards SimplifyRatio. When the result breaks the surface topology (see CheckTopology),
		// the offending vertices are locked and the group is simplified again; after a few rounds the target is
		// relaxed. Returns false when the group is stuck. Temporary locks are removed before returning.
		bool simplifyWithTopologyCheck(const MaterialGeometry& material, const std::vector<uint32_t>& merged, std::vector<uint8_t>& locks,
			const std::vector<uint32_t>& remap, std::vector<uint32_t>& simplified, float& error) {
			constexpr int ratioSteps = 3;
			constexpr int lockRounds = 6;
			const double stuckLimit = double(merged.size()) * m_settings.SimplifyStuckThreshold;
			std::vector<uint32_t> borderIds;
			for (uint32_t vertex : merged) {
				if (locks[vertex] & meshopt_SimplifyVertex_Lock) {
					borderIds.push_back(remap[vertex]);
				}
			}
			std::sort(borderIds.begin(), borderIds.end());
			borderIds.erase(std::unique(borderIds.begin(), borderIds.end()), borderIds.end());
			std::vector<uint32_t> added;
			std::vector<uint32_t> lockIds;
			bool accepted = false;
			bool topologyFailure = false;
			int retries = 0;
			for (int step = 0; step < ratioSteps && !accepted; ++step) {
				const double ratio
					= m_settings.SimplifyRatio + (m_settings.SimplifyStuckThreshold - m_settings.SimplifyRatio) * double(step) / ratioSteps;
				const size_t targetIndexCount = std::max<size_t>(size_t(double(merged.size() / 3u) * ratio), 1u) * 3u;
				for (int round = 0; round < lockRounds; ++round) {
					error = simplifyGroup(material, merged, locks, targetIndexCount, simplified);
					RemoveFinPairs(simplified, merged, remap);
					if (simplified.empty() || double(simplified.size()) > stuckLimit) {
						topologyFailure = false;
						break;
					}
					if (!m_settings.PreserveTopology || CheckTopology(merged, simplified, remap, borderIds, lockIds)) {
						accepted = true;
						break;
					}
					topologyFailure = true;
					retries += 1;
					if (lockIds.front() == ~0u) {
						break;
					}
					for (uint32_t vertex : merged) {
						if (!(locks[vertex] & meshopt_SimplifyVertex_Lock) && std::binary_search(lockIds.begin(), lockIds.end(), remap[vertex])) {
							locks[vertex] |= meshopt_SimplifyVertex_Lock;
							added.push_back(vertex);
						}
					}
				}
			}
			for (uint32_t vertex : added) {
				locks[vertex] &= uint8_t(~meshopt_SimplifyVertex_Lock);
			}
			if (accepted && retries > 0) {
				m_report.RetriedGroupCount += 1u;
			}
			if (!accepted && topologyFailure) {
				m_report.TopologyRejectedGroupCount += 1u;
			}
			return accepted;
		}

		void emitGroup(const MaterialGeometry& material, const WorkCluster* group, size_t groupSize, const std::vector<uint32_t>& pool,
			float parentError, const VMeshSphere& parentBounds) {
			for (size_t i = 0; i < groupSize; ++i) {
				emitCluster(material, group[i], pool, parentError, parentBounds);
			}
		}

		// Cluster LOD DAG (after clusterlod.h by Arseny Kapoulkine, MIT): merge clusters into groups, simplify each
		// group with its border locked, split the result into new clusters, repeat until one cluster is left or no
		// group can be simplified. A cluster is emitted once the group it is merged into is known.
		void buildHierarchy(const MaterialGeometry& material, std::vector<WorkCluster>& clusters, std::vector<uint32_t>& pool) {
			std::vector<uint32_t> remap(material.Vertices.size());
			meshopt_generatePositionRemap(remap.data(), material.Positions.data(), material.Vertices.size(), PositionStride);
			std::vector<uint8_t> locks;
			std::vector<uint8_t> frozen(material.Vertices.size(), 0u);
			std::vector<WorkCluster> pending;
			std::vector<uint32_t> pendingPool;
			std::vector<uint32_t> merged;
			std::vector<uint32_t> simplified;
			std::vector<VMeshSphere> spheres;
			while (clusters.size() > 1u) {
				const std::vector<uint32_t> offsets = partition(material, clusters, pool, remap);
				lockGroupBorders(material, clusters, offsets, pool, remap, frozen, locks);
				pending.clear();
				pendingPool.clear();
				for (size_t group = 0; group + 1u < offsets.size(); ++group) {
					const WorkCluster* members = clusters.data() + offsets[group];
					const size_t memberCount = offsets[group + 1u] - offsets[group];
					merged.clear();
					spheres.clear();
					float childError = 0.0f;
					for (size_t i = 0; i < memberCount; ++i) {
						merged.insert(
							merged.end(), pool.begin() + members[i].IndexOffset, pool.begin() + members[i].IndexOffset + members[i].IndexCount);
						spheres.push_back(members[i].LodBounds);
						childError = std::max(childError, members[i].Error);
					}
					float simplifyError = 0.0f;
					if (!simplifyWithTopologyCheck(material, merged, locks, remap, simplified, simplifyError)) {
						uint32_t deferrals = 0;
						for (size_t i = 0; i < memberCount; ++i) {
							deferrals = std::max(deferrals, members[i].Deferrals);
						}
						if (deferrals < MaxStuckDeferrals) {
							// Carry the clusters unchanged to the next level: they are regrouped with other neighbours
							// (different group borders), which usually unblocks the simplification.
							m_report.DeferredGroupCount += 1u;
							for (size_t i = 0; i < memberCount; ++i) {
								WorkCluster carried = members[i];
								carried.IndexOffset = pendingPool.size();
								carried.Deferrals = deferrals + 1u;
								pendingPool.insert(pendingPool.end(), pool.begin() + members[i].IndexOffset,
									pool.begin() + members[i].IndexOffset + members[i].IndexCount);
								pending.push_back(carried);
							}
							continue;
						}
						// Stuck for good: the group becomes a set of roots. Its vertices stay locked for every later
						// level, otherwise neighbours simplified later could slide their shared border away from it.
						m_report.StuckGroupCount += 1u;
						for (size_t i = 0; i < merged.size(); ++i) {
							frozen[remap[merged[i]]] = 1u;
						}
						emitGroup(material, members, memberCount, pool, VMeshRootError, VMeshSphere {});
						continue;
					}
					const VMeshSphere bounds = MergeSpheres(spheres);
					const float groupError = std::max(childError, simplifyError);
					uint32_t generation = 0;
					for (size_t i = 0; i < memberCount; ++i) {
						generation = std::max(generation, members[i].Generation + 1u);
					}
					const uint32_t groupId = m_nextGroupId++;
					emitGroup(material, members, memberCount, pool, groupError, bounds);
					const size_t firstNew = pending.size();
					clusterize(material, simplified.data(), simplified.size(), groupId, pending, pendingPool);
					for (size_t i = firstNew; i < pending.size(); ++i) {
						pending[i].LodBounds = bounds;
						pending[i].Error = groupError;
						pending[i].Generation = generation;
					}
				}
				clusters.swap(pending);
				pool.swap(pendingPool);
			}
			if (clusters.size() == 1u) {
				emitCluster(material, clusters.front(), pool, VMeshRootError, VMeshSphere {});
			}
		}

		// Fallback mesh as a DAG cut: the coarsest object-space error threshold whose cut still has at least the
		// target triangle count. A cut is crack-free by construction (borders between materials are locked), so the
		// fallback mesh is watertight wherever the source is.
		void buildCutFallback(const MaterialGeometry& material, size_t firstCluster) {
			const size_t endCluster = m_output.Clusters.size();
			size_t leafTriangles = 0;
			std::vector<float> candidates;
			for (size_t i = firstCluster; i < endCluster; ++i) {
				const VMeshCluster& cluster = m_output.Clusters[i];
				leafTriangles += cluster.LodLevel == 0u ? cluster.IndexCount / 3u : 0u;
				candidates.push_back(cluster.Error);
			}
			std::sort(candidates.begin(), candidates.end());
			candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
			const size_t target = fallbackTargetTriangles(leafTriangles);
			auto selected
				= [&](const VMeshCluster& cluster, float threshold) { return cluster.Error <= threshold && threshold < cluster.ParentError; };
			auto trianglesAt = [&](float threshold) {
				size_t triangles = 0;
				for (size_t i = firstCluster; i < endCluster; ++i) {
					triangles += selected(m_output.Clusters[i], threshold) ? m_output.Clusters[i].IndexCount / 3u : 0u;
				}
				return triangles;
			};
			// trianglesAt does not increase with the threshold; candidates[0] == 0 selects level 0.
			size_t low = 0;
			size_t high = candidates.size() - 1u;
			while (low < high) {
				const size_t middle = (low + high + 1u) / 2u;
				if (trianglesAt(candidates[middle]) >= target) {
					low = middle;
				}
				else {
					high = middle - 1u;
				}
			}
			const float threshold = candidates[low];
			VMeshFallbackRange range;
			range.MaterialSlot = material.Slot;
			range.IndexOffset = uint32_t(m_output.Indices.size());
			for (size_t i = firstCluster; i < endCluster; ++i) {
				const VMeshCluster cluster = m_output.Clusters[i];
				if (selected(cluster, threshold)) {
					for (uint32_t j = 0; j < cluster.IndexCount; ++j) {
						m_output.Indices.push_back(m_output.Indices[cluster.IndexOffset + j]);
					}
				}
			}
			range.IndexCount = uint32_t(m_output.Indices.size()) - range.IndexOffset;
			m_output.FallbackRanges.push_back(range);
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
		uint32_t m_nextGroupId = 0;
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
