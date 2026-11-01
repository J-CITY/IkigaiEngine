#include "vmeshIO.h"

#include "vmeshEncoding.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace IKIGAI::VG {
namespace {
	[[noreturn]] void Fail(const std::string& message) {
		throw VMeshFormatError(".vmesh: " + message);
	}

	template <typename... Args> std::string Format(const Args&... args) {
		std::ostringstream stream;
		(stream << ... << args);
		return stream.str();
	}

	bool IsFinite(float value) {
		return std::isfinite(value);
	}

	bool IsFiniteSphere(const VMeshSphere& sphere) {
		return IsFinite(sphere.Center[0]) && IsFinite(sphere.Center[1]) && IsFinite(sphere.Center[2]) && IsFinite(sphere.Radius)
			&& sphere.Radius >= 0.0f;
	}

	bool SameSphere(const VMeshSphere& a, const VMeshSphere& b) {
		return a.Center[0] == b.Center[0] && a.Center[1] == b.Center[1] && a.Center[2] == b.Center[2] && a.Radius == b.Radius;
	}

	uint64_t AlignUp(uint64_t value, uint64_t alignment) {
		return (value + alignment - 1u) / alignment * alignment;
	}

	void CheckIndexRange(uint64_t offset, uint64_t count, size_t indexCount, const std::string& what) {
		if (count % 3u != 0u) {
			Fail(Format(what, ": IndexCount ", count, " is not a multiple of 3"));
		}
		if (offset > indexCount || count > indexCount - offset) {
			Fail(Format(what, ": index range [", offset, ", ", offset + count, ") exceeds the Indices section (", indexCount, " indices)"));
		}
	}

	void CheckHeaderIdentity(const VMeshHeader& header) {
		if (header.Magic != VMeshMagic) {
			Fail(Format("bad magic 0x", std::hex, header.Magic, " (expected 0x", VMeshMagic, ")"));
		}
		if (header.Version != VMeshVersion) {
			Fail(Format("unsupported version ", header.Version, " (expected ", VMeshVersion, ")"));
		}
		if (header.VertexLayoutVersion != VMeshVertexLayoutVersion) {
			Fail(Format("unsupported vertex layout version ", header.VertexLayoutVersion, " (expected ", VMeshVertexLayoutVersion, ")"));
		}
		if (header.Flags & ~uint32_t(VMESH_FLAG_HAS_TANGENTS)) {
			Fail(Format("unknown header flags 0x", std::hex, header.Flags));
		}
	}

	void ValidateHeaderValues(const VMeshData& data) {
		const VMeshHeader& header = data.Header;
		CheckHeaderIdentity(header);
		if (header.MaterialSlotCount == 0u) {
			Fail("MaterialSlotCount is 0");
		}
		if (header.LodLevelCount == 0u) {
			Fail("LodLevelCount is 0");
		}
		for (int axis = 0; axis < 3; ++axis) {
			if (!IsFinite(header.BoundsMin[axis]) || !IsFinite(header.BoundsMax[axis])) {
				Fail("header bounds are not finite");
			}
			if (header.BoundsMin[axis] > header.BoundsMax[axis]) {
				Fail(Format("header BoundsMin > BoundsMax on axis ", axis));
			}
		}
		if (!IsFiniteSphere(header.BoundingSphere)) {
			Fail("header bounding sphere is not finite or has a negative radius");
		}
	}

	void ValidateVertices(const VMeshData& data) {
		if (data.Vertices.empty()) {
			Fail("Vertices section is empty");
		}
		for (size_t i = 0; i < data.Vertices.size(); ++i) {
			const VMeshVertex& vertex = data.Vertices[i];
			if (!IsFinite(vertex.Position[0]) || !IsFinite(vertex.Position[1]) || !IsFinite(vertex.Position[2]) || !IsFinite(vertex.Uv[0])
				|| !IsFinite(vertex.Uv[1])) {
				Fail(Format("vertex ", i, " has a non-finite position or uv"));
			}
			if (vertex.Tangent & VMeshTangentReservedBit) {
				Fail(Format("vertex ", i, " sets the reserved tangent bit 30"));
			}
		}
	}

	void ValidateIndices(const VMeshData& data) {
		if (data.Indices.empty()) {
			Fail("Indices section is empty");
		}
		if (data.Indices.size() % 3u != 0u) {
			Fail(Format("Indices section count ", data.Indices.size(), " is not a multiple of 3"));
		}
		const uint32_t vertexCount = uint32_t(data.Vertices.size());
		for (size_t i = 0; i < data.Indices.size(); ++i) {
			if (data.Indices[i] >= vertexCount) {
				Fail(Format("index ", i, " = ", data.Indices[i], " is out of range (", vertexCount, " vertices)"));
			}
		}
	}

	void ValidateClusters(const VMeshData& data) {
		const VMeshHeader& header = data.Header;
		if (data.Clusters.empty()) {
			Fail("Clusters section is empty");
		}
		struct GroupReference {
			size_t FirstCluster = 0;
		};
		std::unordered_map<uint32_t, GroupReference> groups;
		uint32_t maxLevel = 0;
		for (size_t i = 0; i < data.Clusters.size(); ++i) {
			const VMeshCluster& cluster = data.Clusters[i];
			const std::string name = Format("cluster ", i);
			if (cluster.IndexCount == 0u) {
				Fail(name + ": IndexCount is 0");
			}
			if (cluster.IndexCount > 3u * VMeshMaxClusterTriangles) {
				Fail(Format(name, ": ", cluster.IndexCount / 3u, " triangles exceed the limit of ", VMeshMaxClusterTriangles));
			}
			CheckIndexRange(cluster.IndexOffset, cluster.IndexCount, data.Indices.size(), name);
			if (cluster.MaterialSlot >= header.MaterialSlotCount) {
				Fail(Format(name, ": MaterialSlot ", cluster.MaterialSlot, " >= MaterialSlotCount ", header.MaterialSlotCount));
			}
			if (cluster.LodLevel >= header.LodLevelCount) {
				Fail(Format(name, ": LodLevel ", cluster.LodLevel, " >= LodLevelCount ", header.LodLevelCount));
			}
			maxLevel = std::max(maxLevel, cluster.LodLevel);
			if (cluster.Flags & ~uint32_t(VMESH_CLUSTER_LEAF | VMESH_CLUSTER_ROOT)) {
				Fail(Format(name, ": unknown flags 0x", std::hex, cluster.Flags));
			}
			const bool leaf = cluster.LodLevel == 0u;
			if (leaf != ((cluster.Flags & VMESH_CLUSTER_LEAF) != 0u)) {
				Fail(name + ": the LEAF flag does not match LodLevel == 0");
			}
			const bool root = cluster.ParentError == VMeshRootError;
			if (root != ((cluster.Flags & VMESH_CLUSTER_ROOT) != 0u)) {
				Fail(name + ": the ROOT flag does not match ParentError == VMeshRootError");
			}
			if (!IsFinite(cluster.Error) || cluster.Error < 0.0f) {
				Fail(Format(name, ": Error ", cluster.Error, " is not a finite non-negative value"));
			}
			if (!root && !IsFinite(cluster.ParentError)) {
				Fail(Format(name, ": ParentError is not finite"));
			}
			if (!(cluster.ParentError >= cluster.Error)) {
				Fail(Format(name, ": ParentError ", cluster.ParentError, " < Error ", cluster.Error, " (error must not decrease up the DAG)"));
			}
			if (leaf && cluster.Error != 0.0f) {
				Fail(Format(name, ": level 0 cluster has Error ", cluster.Error, " (expected 0)"));
			}
			if (leaf != (cluster.GroupId == VMeshLeafGroupId)) {
				Fail(Format(name, ": GroupId ", cluster.GroupId, " does not match LodLevel ", cluster.LodLevel));
			}
			if (!IsFiniteSphere(cluster.Bounds) || !IsFiniteSphere(cluster.LodBounds) || !IsFiniteSphere(cluster.ParentLodBounds)) {
				Fail(name + ": a bounding sphere is not finite or has a negative radius");
			}
			if (!IsFinite(cluster.ConeAxis[0]) || !IsFinite(cluster.ConeAxis[1]) || !IsFinite(cluster.ConeAxis[2]) || !IsFinite(cluster.ConeCutoff)) {
				Fail(name + ": the normal cone is not finite");
			}
			if (!leaf) {
				const auto [it, inserted] = groups.emplace(cluster.GroupId, GroupReference { i });
				if (!inserted) {
					const VMeshCluster& first = data.Clusters[it->second.FirstCluster];
					if (first.Error != cluster.Error || !SameSphere(first.LodBounds, cluster.LodBounds) || first.LodLevel != cluster.LodLevel) {
						Fail(Format(name, ": Error/LodBounds/LodLevel differ from cluster ", it->second.FirstCluster, " of the same group ",
							cluster.GroupId));
					}
				}
			}
		}
		if (maxLevel + 1u != header.LodLevelCount) {
			Fail(Format("LodLevelCount ", header.LodLevelCount, " does not match the highest cluster level ", maxLevel));
		}
	}

	void ValidateFallback(const VMeshData& data) {
		for (size_t i = 0; i < data.FallbackRanges.size(); ++i) {
			const VMeshFallbackRange& range = data.FallbackRanges[i];
			const std::string name = Format("fallback range ", i);
			if (range.MaterialSlot >= data.Header.MaterialSlotCount) {
				Fail(Format(name, ": MaterialSlot ", range.MaterialSlot, " >= MaterialSlotCount ", data.Header.MaterialSlotCount));
			}
			CheckIndexRange(range.IndexOffset, range.IndexCount, data.Indices.size(), name);
		}
	}

	template <typename T> VMeshSection MakeSection(uint64_t& cursor, size_t count) {
		VMeshSection section;
		cursor = AlignUp(cursor, VMeshSectionAlignment);
		section.Offset = cursor;
		section.Count = uint32_t(count);
		section.Stride = uint32_t(sizeof(T));
		section.Size = uint64_t(count) * sizeof(T);
		cursor += section.Size;
		return section;
	}

	template <typename T> void CheckSection(const VMeshSection& section, size_t fileSize, const char* name) {
		if (section.Stride != sizeof(T)) {
			Fail(Format(name, " section stride ", section.Stride, " (expected ", sizeof(T), ")"));
		}
		if (section.Size != uint64_t(section.Count) * section.Stride) {
			Fail(Format(name, " section size ", section.Size, " != Count ", section.Count, " * Stride ", section.Stride));
		}
		if (section.Offset % VMeshSectionAlignment != 0u) {
			Fail(Format(name, " section offset ", section.Offset, " is not 16-byte aligned"));
		}
		if (section.Offset < sizeof(VMeshHeader)) {
			Fail(Format(name, " section offset ", section.Offset, " overlaps the header"));
		}
		if (section.Offset > fileSize || section.Size > fileSize - section.Offset) {
			Fail(Format(name, " section [", section.Offset, ", +", section.Size, ") exceeds the file size ", fileSize));
		}
	}

	template <typename T> void CopySection(const uint8_t* bytes, const VMeshSection& section, std::vector<T>& out) {
		out.resize(section.Count);
		if (section.Size) {
			std::memcpy(out.data(), bytes + section.Offset, size_t(section.Size));
		}
	}

	template <typename T> void AppendSection(std::vector<uint8_t>& bytes, const VMeshSection& section, const std::vector<T>& values) {
		if (bytes.size() < section.Offset) {
			bytes.resize(size_t(section.Offset), 0u);
		}
		if (section.Size) {
			const size_t start = bytes.size();
			bytes.resize(start + size_t(section.Size));
			std::memcpy(bytes.data() + start, values.data(), size_t(section.Size));
		}
	}
}

VMeshFormatError::VMeshFormatError(const std::string& message)
	: std::runtime_error(message) { }

void ValidateVMesh(const VMeshData& data) {
	ValidateHeaderValues(data);
	ValidateVertices(data);
	ValidateIndices(data);
	ValidateClusters(data);
	ValidateFallback(data);
}

void UpdateVMeshSections(VMeshData& data) {
	uint64_t cursor = sizeof(VMeshHeader);
	data.Header.Vertices = MakeSection<VMeshVertex>(cursor, data.Vertices.size());
	data.Header.Indices = MakeSection<uint32_t>(cursor, data.Indices.size());
	data.Header.Clusters = MakeSection<VMeshCluster>(cursor, data.Clusters.size());
	data.Header.FallbackRanges = MakeSection<VMeshFallbackRange>(cursor, data.FallbackRanges.size());
}

std::vector<uint8_t> WriteVMesh(const VMeshData& data) {
	constexpr size_t maxCount = std::numeric_limits<uint32_t>::max();
	if (data.Vertices.size() > maxCount || data.Indices.size() > maxCount || data.Clusters.size() > maxCount
		|| data.FallbackRanges.size() > maxCount) {
		Fail("a section has more than 2^32-1 elements");
	}
	ValidateVMesh(data);
	VMeshHeader header = data.Header;
	uint64_t cursor = sizeof(VMeshHeader);
	header.Vertices = MakeSection<VMeshVertex>(cursor, data.Vertices.size());
	header.Indices = MakeSection<uint32_t>(cursor, data.Indices.size());
	header.Clusters = MakeSection<VMeshCluster>(cursor, data.Clusters.size());
	header.FallbackRanges = MakeSection<VMeshFallbackRange>(cursor, data.FallbackRanges.size());

	std::vector<uint8_t> bytes;
	bytes.reserve(size_t(cursor));
	bytes.resize(sizeof(VMeshHeader));
	std::memcpy(bytes.data(), &header, sizeof(VMeshHeader));
	AppendSection(bytes, header.Vertices, data.Vertices);
	AppendSection(bytes, header.Indices, data.Indices);
	AppendSection(bytes, header.Clusters, data.Clusters);
	AppendSection(bytes, header.FallbackRanges, data.FallbackRanges);
	bytes.resize(size_t(cursor), 0u);
	return bytes;
}

void WriteVMeshFile(const std::string& nativePath, const VMeshData& data) {
	const std::vector<uint8_t> bytes = WriteVMesh(data);
	const std::filesystem::path path(nativePath);
	if (path.has_parent_path()) {
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
	}
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		throw std::runtime_error("Cannot open " + nativePath + " for writing");
	}
	file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	if (!file) {
		throw std::runtime_error("Cannot write " + nativePath);
	}
}

VMeshData ReadVMesh(const uint8_t* bytes, size_t size) {
	if (!bytes || size < sizeof(VMeshHeader)) {
		Fail(Format("file is ", size, " bytes, smaller than the ", sizeof(VMeshHeader), "-byte header"));
	}
	VMeshData data;
	std::memcpy(&data.Header, bytes, sizeof(VMeshHeader));
	CheckHeaderIdentity(data.Header);
	CheckSection<VMeshVertex>(data.Header.Vertices, size, "Vertices");
	CheckSection<uint32_t>(data.Header.Indices, size, "Indices");
	CheckSection<VMeshCluster>(data.Header.Clusters, size, "Clusters");
	CheckSection<VMeshFallbackRange>(data.Header.FallbackRanges, size, "FallbackRanges");
	const VMeshSection* sections[] = { &data.Header.Vertices, &data.Header.Indices, &data.Header.Clusters, &data.Header.FallbackRanges };
	for (size_t a = 0; a < 4; ++a) {
		for (size_t b = a + 1; b < 4; ++b) {
			const VMeshSection& first = *sections[a];
			const VMeshSection& second = *sections[b];
			if (first.Size && second.Size && first.Offset < second.Offset + second.Size && second.Offset < first.Offset + first.Size) {
				Fail(Format("sections ", a, " and ", b, " overlap"));
			}
		}
	}
	CopySection(bytes, data.Header.Vertices, data.Vertices);
	CopySection(bytes, data.Header.Indices, data.Indices);
	CopySection(bytes, data.Header.Clusters, data.Clusters);
	CopySection(bytes, data.Header.FallbackRanges, data.FallbackRanges);
	ValidateVMesh(data);
	return data;
}

VMeshData ReadVMesh(const std::vector<uint8_t>& bytes) {
	return ReadVMesh(bytes.data(), bytes.size());
}

VMeshData ReadVMeshFile(const std::string& nativePath) {
	std::ifstream file(std::filesystem::path(nativePath), std::ios::binary | std::ios::ate);
	if (!file) {
		throw std::runtime_error("Cannot open " + nativePath);
	}
	const std::streamsize size = file.tellg();
	if (size < 0) {
		throw std::runtime_error("Cannot read " + nativePath);
	}
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	file.seekg(0);
	if (size && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
		throw std::runtime_error("Cannot read " + nativePath);
	}
	return ReadVMesh(bytes);
}

uint64_t HashVMeshBytes(const void* bytes, size_t size, uint64_t seed) {
	const auto* data = static_cast<const uint8_t*>(bytes);
	uint64_t hash = seed;
	for (size_t i = 0; i < size; ++i) {
		hash ^= data[i];
		hash *= 0x100000001B3ull;
	}
	return hash;
}

VMeshStatistics ComputeVMeshStatistics(const VMeshData& data) {
	VMeshStatistics statistics;
	statistics.VertexCount = uint32_t(data.Vertices.size());
	statistics.IndexCount = uint32_t(data.Indices.size());
	statistics.ClusterCount = uint32_t(data.Clusters.size());
	statistics.LodLevelCount = data.Header.LodLevelCount;
	statistics.ClustersPerLevel.assign(data.Header.LodLevelCount, 0u);
	statistics.TrianglesPerLevel.assign(data.Header.LodLevelCount, 0u);
	std::unordered_set<uint32_t> groups;
	for (const VMeshCluster& cluster : data.Clusters) {
		if (cluster.LodLevel >= statistics.ClustersPerLevel.size()) {
			statistics.ClustersPerLevel.resize(cluster.LodLevel + 1u, 0u);
			statistics.TrianglesPerLevel.resize(cluster.LodLevel + 1u, 0u);
		}
		statistics.ClustersPerLevel[cluster.LodLevel] += 1u;
		statistics.TrianglesPerLevel[cluster.LodLevel] += cluster.IndexCount / 3u;
		if (cluster.Flags & VMESH_CLUSTER_LEAF) {
			statistics.LeafClusterCount += 1u;
			statistics.LeafTriangleCount += cluster.IndexCount / 3u;
		}
		if (cluster.Flags & VMESH_CLUSTER_ROOT) {
			statistics.RootClusterCount += 1u;
		}
		if (cluster.GroupId != VMeshLeafGroupId) {
			groups.insert(cluster.GroupId);
		}
	}
	statistics.GroupCount = uint32_t(groups.size());
	for (const VMeshFallbackRange& range : data.FallbackRanges) {
		statistics.FallbackTriangleCount += range.IndexCount / 3u;
	}
	return statistics;
}
}
