#include "vmeshIO.h"

#include "vmeshEncoding.h"

#include <algorithm>
#include <array>
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

	void ValidateHeaderValues(const VMeshHeader& header) {
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

	void ValidateVertices(std::span<const VMeshVertex> vertices) {
		if (vertices.empty()) {
			Fail("Vertices section is empty");
		}
		for (size_t i = 0; i < vertices.size(); ++i) {
			const VMeshVertex& vertex = vertices[i];
			if (!IsFinite(vertex.Position[0]) || !IsFinite(vertex.Position[1]) || !IsFinite(vertex.Position[2]) || !IsFinite(vertex.Uv[0])
				|| !IsFinite(vertex.Uv[1])) {
				Fail(Format("vertex ", i, " has a non-finite position or uv"));
			}
			if (vertex.Tangent & VMeshTangentReservedBit) {
				Fail(Format("vertex ", i, " sets the reserved tangent bit 30"));
			}
		}
	}

	void ValidateIndices(std::span<const uint32_t> indices, size_t vertexCount) {
		if (indices.empty()) {
			Fail("Indices section is empty");
		}
		if (indices.size() % 3u != 0u) {
			Fail(Format("Indices section count ", indices.size(), " is not a multiple of 3"));
		}
		for (size_t i = 0; i < indices.size(); ++i) {
			if (indices[i] >= vertexCount) {
				Fail(Format("index ", i, " = ", indices[i], " is out of range (", vertexCount, " vertices)"));
			}
		}
	}

	void ValidateClusters(const VMeshHeader& header, std::span<const VMeshCluster> clusters) {
		if (clusters.empty()) {
			Fail("Clusters section is empty");
		}
		struct GroupReference {
			size_t FirstCluster = 0;
		};
		std::unordered_map<uint32_t, GroupReference> groups;
		uint32_t maxLevel = 0;
		for (size_t i = 0; i < clusters.size(); ++i) {
			const VMeshCluster& cluster = clusters[i];
			const std::string name = Format("cluster ", i);
			if (cluster.IndexCount == 0u) {
				Fail(name + ": IndexCount is 0");
			}
			if (cluster.IndexCount > 3u * VMeshMaxClusterTriangles) {
				Fail(Format(name, ": ", cluster.IndexCount / 3u, " triangles exceed the limit of ", VMeshMaxClusterTriangles));
			}
			if (cluster.IndexCount % 3u != 0u) {
				Fail(name + ": IndexCount is not a multiple of 3");
			}
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
					const VMeshCluster& first = clusters[it->second.FirstCluster];
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
		if (count > UINT32_MAX) {
			Fail("a section has more than 2^32-1 elements");
		}
		VMeshSection section;
		cursor = AlignUp(cursor, VMeshSectionAlignment);
		section.Offset = cursor;
		section.Count = uint32_t(count);
		section.Stride = uint32_t(sizeof(T));
		section.Size = uint64_t(count) * sizeof(T);
		cursor += section.Size;
		return section;
	}

	template <typename T>
	void CheckSection(const VMeshSection& section, uint64_t fileSize, const char* name, uint64_t headerSize = sizeof(VMeshHeader)) {
		if (section.Stride != sizeof(T)) {
			Fail(Format(name, " section stride ", section.Stride, " (expected ", sizeof(T), ")"));
		}
		if (section.Size != uint64_t(section.Count) * section.Stride) {
			Fail(Format(name, " section size ", section.Size, " != Count ", section.Count, " * Stride ", section.Stride));
		}
		if (section.Offset % VMeshSectionAlignment != 0u) {
			Fail(Format(name, " section offset ", section.Offset, " is not 16-byte aligned"));
		}
		if (section.Offset < headerSize) {
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

	void WriteVMeshBytes(const std::string& nativePath, std::span<const uint8_t> bytes);
}

VMeshFormatError::VMeshFormatError(const std::string& message)
	: std::runtime_error(message) { }

void ValidateVMesh(const VMeshData& data) {
	ValidateHeaderValues(data.Header);
	ValidateVertices(data.Vertices);
	ValidateIndices(data.Indices, data.Vertices.size());
	ValidateClusters(data.Header, data.Clusters);
	for (size_t i = 0; i < data.Clusters.size(); ++i) {
		const auto& cluster = data.Clusters[i];
		CheckIndexRange(cluster.IndexOffset, cluster.IndexCount, data.Indices.size(), Format("cluster ", i));
	}
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
	WriteVMeshBytes(nativePath, WriteVMesh(data));
}

namespace {
	uint64_t MetadataChecksum(VMeshPagedHeader header, std::span<const uint8_t> metadata) {
		header.MetadataChecksum = 0;
		return HashVMeshBytes(metadata.data() + sizeof(header), metadata.size() - sizeof(header), HashVMeshBytes(&header, sizeof(header)));
	}

	uint64_t PageSize(uint32_t vertices, uint32_t indices) {
		return uint64_t(vertices) * sizeof(VMeshVertex) + uint64_t(indices) * sizeof(uint32_t);
	}

	void CheckPagedSections(const VMeshPagedHeader& header) {
		const uint64_t size = header.MetadataSize;
		constexpr auto first = sizeof(VMeshPagedHeader);
		CheckSection<VMeshVertex>(header.Asset.Vertices, size, "fallback vertices", first);
		CheckSection<uint32_t>(header.Asset.Indices, size, "fallback indices", first);
		CheckSection<VMeshCluster>(header.Asset.Clusters, size, "clusters", first);
		CheckSection<VMeshFallbackRange>(header.Asset.FallbackRanges, size, "fallback ranges", first);
		CheckSection<uint32_t>(header.ClusterPages, size, "cluster pages", first);
		CheckSection<VMeshTransition>(header.Transitions, size, "transitions", first);
		CheckSection<uint32_t>(header.TransitionClusters, size, "transition clusters", first);
		CheckSection<uint32_t>(header.Roots, size, "root cut", first);
		CheckSection<VMeshPageEntry>(header.Pages, size, "page directory", first);
		const VMeshSection sections[] = { header.Asset.Vertices, header.Asset.Indices, header.Asset.Clusters, header.Asset.FallbackRanges,
			header.ClusterPages, header.Transitions, header.TransitionClusters, header.Roots, header.Pages };
		uint64_t end = first;
		for (const auto& section : sections) {
			if (section.Offset != AlignUp(end, VMeshSectionAlignment)) {
				Fail("paged metadata sections are unordered, overlapping, or padded incorrectly");
			}
			end = section.Offset + section.Size;
		}
		if (end != size || !header.Pages.Count || !header.Roots.Count || !header.Asset.Clusters.Count || !header.Asset.FallbackRanges.Count
			|| header.ClusterPages.Count != header.Asset.Clusters.Count || header.Pages.Count > header.Asset.Clusters.Count
			|| header.Roots.Count > header.Asset.Clusters.Count || header.Transitions.Count > header.Asset.Clusters.Count
			|| header.TransitionClusters.Count > uint64_t(header.Asset.Clusters.Count) * 2u) {
			Fail("invalid paged metadata counts or size");
		}
	}

	void ValidatePageDirectory(const VMeshPagedMetadata& metadata) {
		uint64_t end = metadata.Header.MetadataSize;
		std::vector<uint32_t> indexEnds(metadata.Pages.size(), 0u);
		for (const auto& page : metadata.Pages) {
			if (!page.VertexCount || !page.IndexCount || page.IndexCount % 3u != 0u || page.Size > VMeshMaxPageBytes
				|| page.Size != PageSize(page.VertexCount, page.IndexCount) || page.Offset % VMeshSectionAlignment != 0u || page.Offset < end
				|| page.Offset - end >= VMeshSectionAlignment || page.Offset > metadata.Header.FileSize
				|| page.Size > metadata.Header.FileSize - page.Offset) {
				Fail("invalid page directory entry");
			}
			end = page.Offset + page.Size;
		}
		if (end != metadata.Header.FileSize) {
			Fail("page directory does not reach the end of the file");
		}
		for (size_t id = 0; id < metadata.Clusters.size(); ++id) {
			const auto pageId = metadata.ClusterPages[id];
			const auto& cluster = metadata.Clusters[id];
			if (pageId >= metadata.Pages.size() || cluster.IndexOffset != indexEnds[pageId]) {
				Fail("invalid cluster page or overlapping cluster index range");
			}
			CheckIndexRange(cluster.IndexOffset, cluster.IndexCount, metadata.Pages[pageId].IndexCount, "page cluster");
			indexEnds[pageId] += cluster.IndexCount;
		}
		for (size_t page = 0; page < metadata.Pages.size(); ++page) {
			if (indexEnds[page] != metadata.Pages[page].IndexCount) {
				Fail("page indices do not match its clusters");
			}
		}
	}

	bool ContainsSphere(const VMeshSphere& outer, const VMeshSphere& inner) {
		const double x = double(outer.Center[0]) - inner.Center[0];
		const double y = double(outer.Center[1]) - inner.Center[1];
		const double z = double(outer.Center[2]) - inner.Center[2];
		return std::sqrt(x * x + y * y + z * z) + inner.Radius <= double(outer.Radius) * (1.0 + 1e-5);
	}

	void ValidateTransitions(const VMeshPagedMetadata& metadata) {
		std::vector<bool> produced(metadata.Clusters.size(), false);
		std::vector<bool> consumed(metadata.Clusters.size(), false);
		uint64_t cursor = 0;
		const auto checkRange = [&](uint32_t first, uint32_t count) {
			if (!count || first != cursor || uint64_t(first) + count > metadata.TransitionClusters.size()) {
				Fail("invalid transition cluster range");
			}
			cursor += count;
		};
		for (size_t groupId = 0; groupId < metadata.Transitions.size(); ++groupId) {
			const auto& transition = metadata.Transitions[groupId];
			checkRange(transition.CoarseFirst, transition.CoarseCount);
			checkRange(transition.FineFirst, transition.FineCount);
			const auto coarseId = metadata.TransitionClusters[transition.CoarseFirst];
			if (coarseId >= metadata.Clusters.size()) {
				Fail("invalid coarse transition cluster");
			}
			const auto& coarse = metadata.Clusters[coarseId];
			for (uint32_t i = 0; i < transition.CoarseCount; ++i) {
				const auto clusterId = metadata.TransitionClusters[transition.CoarseFirst + i];
				if (clusterId >= metadata.Clusters.size() || produced[clusterId] || metadata.Clusters[clusterId].GroupId != groupId
					|| metadata.Clusters[clusterId].MaterialSlot != coarse.MaterialSlot) {
					Fail("invalid transition producer");
				}
				produced[clusterId] = true;
			}
			for (uint32_t i = 0; i < transition.FineCount; ++i) {
				const auto clusterId = metadata.TransitionClusters[transition.FineFirst + i];
				if (clusterId >= metadata.Clusters.size() || consumed[clusterId]) {
					Fail("invalid transition consumer");
				}
				const auto& fine = metadata.Clusters[clusterId];
				if (fine.Flags & VMESH_CLUSTER_ROOT || fine.MaterialSlot != coarse.MaterialSlot || fine.LodLevel >= coarse.LodLevel
					|| (fine.GroupId != VMeshLeafGroupId && fine.GroupId >= groupId) || fine.ParentError != coarse.Error
					|| !SameSphere(fine.ParentLodBounds, coarse.LodBounds) || !ContainsSphere(coarse.LodBounds, fine.LodBounds)) {
					Fail("invalid transition refinement");
				}
				consumed[clusterId] = true;
			}
		}
		if (cursor != metadata.TransitionClusters.size()) {
			Fail("unused transition cluster references");
		}
		size_t root = 0;
		for (size_t id = 0; id < metadata.Clusters.size(); ++id) {
			const auto& cluster = metadata.Clusters[id];
			const bool isRoot = (cluster.Flags & VMESH_CLUSTER_ROOT) != 0u;
			const bool isLeaf = (cluster.Flags & VMESH_CLUSTER_LEAF) != 0u;
			if (produced[id] == isLeaf || consumed[id] == isRoot) {
				Fail("incomplete transition membership");
			}
			if (isRoot && (root >= metadata.Roots.size() || metadata.Roots[root++] != id)) {
				Fail("root cut does not match root clusters");
			}
		}
		if (root != metadata.Roots.size()) {
			Fail("root cut has extra references");
		}
	}

	template <typename GetPage> VMeshData MaterializePaged(const VMeshPagedMetadata& metadata, GetPage getPage) {
		auto data = metadata.FallbackData;
		std::vector<uint32_t> indexBases;
		indexBases.reserve(metadata.Pages.size());
		for (size_t id = 0; id < metadata.Pages.size(); ++id) {
			const auto& entry = metadata.Pages[id];
			const auto& page = getPage(static_cast<uint32_t>(id));
			if (page.Vertices.size() != entry.VertexCount || page.Indices.size() != entry.IndexCount
				|| data.Vertices.size() + uint64_t(page.Vertices.size()) > UINT32_MAX
				|| data.Indices.size() + uint64_t(page.Indices.size()) > UINT32_MAX) {
				Fail("materialized geometry has invalid counts or exceeds 32-bit addressing");
			}
			const auto vertexBase = static_cast<uint32_t>(data.Vertices.size());
			indexBases.push_back(static_cast<uint32_t>(data.Indices.size()));
			data.Vertices.insert(data.Vertices.end(), page.Vertices.begin(), page.Vertices.end());
			for (const uint32_t index : page.Indices) {
				data.Indices.push_back(vertexBase + index);
			}
		}
		data.Clusters = metadata.Clusters;
		for (size_t id = 0; id < data.Clusters.size(); ++id) {
			data.Clusters[id].IndexOffset += indexBases[metadata.ClusterPages[id]];
		}
		data.ClusterConsumers.assign(data.Clusters.size(), UINT32_MAX);
		for (size_t group = 0; group < metadata.Transitions.size(); ++group) {
			const auto& transition = metadata.Transitions[group];
			for (uint32_t i = 0; i < transition.FineCount; ++i) {
				data.ClusterConsumers[metadata.TransitionClusters[transition.FineFirst + i]] = static_cast<uint32_t>(group);
			}
		}
		UpdateVMeshSections(data);
		return data;
	}

	VMeshData MaterializePaged(std::span<const uint8_t> bytes) {
		const auto header = ReadVMeshPagedHeader(bytes, bytes.size());
		const auto metadata = ReadVMeshPagedMetadata(bytes.first(static_cast<size_t>(header.MetadataSize)), bytes.size());
		return MaterializePaged(metadata, [&](uint32_t pageId) {
			const auto& entry = metadata.Pages[pageId];
			return ReadVMeshPage(metadata, pageId, bytes.subspan(static_cast<size_t>(entry.Offset), static_cast<size_t>(entry.Size)));
		});
	}
}

VMeshPagedHeader ReadVMeshPagedHeader(std::span<const uint8_t> bytes, uint64_t fileSize) {
	VMeshPagedHeader header;
	if (bytes.size() < sizeof(header)) {
		Fail("incomplete paged header");
	}
	std::memcpy(&header, bytes.data(), sizeof(header));
	if (header.Magic != VMeshMagic || header.Version != VMeshPagedVersion || header.FileSize != fileSize || header.MetadataSize < sizeof(header)
		|| header.MetadataSize > fileSize || header.MetadataSize > 512ull * 1024u * 1024u) {
		Fail("invalid paged header identity or file size");
	}
	ValidateHeaderValues(header.Asset);
	CheckPagedSections(header);
	return header;
}

VMeshPagedMetadata ReadVMeshPagedMetadata(std::span<const uint8_t> bytes, uint64_t fileSize) {
	VMeshPagedMetadata metadata;
	metadata.Header = ReadVMeshPagedHeader(bytes, fileSize);
	const auto& header = metadata.Header;
	if (bytes.size() != header.MetadataSize || MetadataChecksum(header, bytes) != header.MetadataChecksum) {
		Fail("invalid paged metadata size or checksum");
	}
	metadata.FallbackData.Header = header.Asset;
	CopySection(bytes.data(), header.Asset.Vertices, metadata.FallbackData.Vertices);
	CopySection(bytes.data(), header.Asset.Indices, metadata.FallbackData.Indices);
	CopySection(bytes.data(), header.Asset.FallbackRanges, metadata.FallbackData.FallbackRanges);
	CopySection(bytes.data(), header.Asset.Clusters, metadata.Clusters);
	CopySection(bytes.data(), header.ClusterPages, metadata.ClusterPages);
	CopySection(bytes.data(), header.Transitions, metadata.Transitions);
	CopySection(bytes.data(), header.TransitionClusters, metadata.TransitionClusters);
	CopySection(bytes.data(), header.Roots, metadata.Roots);
	CopySection(bytes.data(), header.Pages, metadata.Pages);
	ValidateVertices(metadata.FallbackData.Vertices);
	ValidateIndices(metadata.FallbackData.Indices, metadata.FallbackData.Vertices.size());
	ValidateFallback(metadata.FallbackData);
	ValidateClusters(header.Asset, metadata.Clusters);
	ValidatePageDirectory(metadata);
	ValidateTransitions(metadata);
	return metadata;
}

VMeshPageData ReadVMeshPage(const VMeshPagedMetadata& metadata, uint32_t pageId, std::span<const uint8_t> bytes) {
	if (pageId >= metadata.Pages.size()) {
		Fail("page ID out of range");
	}
	const auto& page = metadata.Pages[pageId];
	if (bytes.size() != page.Size || bytes.size() != PageSize(page.VertexCount, page.IndexCount)
		|| HashVMeshBytes(bytes.data(), bytes.size()) != page.Checksum || !page.VertexCount || !page.IndexCount) {
		Fail(Format("invalid size or checksum for page ", pageId));
	}
	VMeshPageData data;
	data.Vertices.resize(page.VertexCount);
	data.Indices.resize(page.IndexCount);
	const size_t vertexBytes = data.Vertices.size() * sizeof(VMeshVertex);
	std::memcpy(data.Vertices.data(), bytes.data(), vertexBytes);
	std::memcpy(data.Indices.data(), bytes.data() + vertexBytes, data.Indices.size() * sizeof(uint32_t));
	ValidateVertices(data.Vertices);
	ValidateIndices(data.Indices, data.Vertices.size());
	return data;
}

VMeshData MaterializeVMeshPages(const VMeshPagedMetadata& metadata, std::span<const VMeshPageData> pages) {
	if (pages.size() != metadata.Pages.size()) {
		Fail("materialization requires every validated page");
	}
	return MaterializePaged(metadata, [&](uint32_t pageId) -> const VMeshPageData& { return pages[pageId]; });
}

VMeshData MaterializeVMeshPages(const VMeshPagedMetadata& metadata, const std::function<VMeshPageData(uint32_t)>& readPage) {
	return MaterializePaged(metadata, readPage);
}

namespace {
	void AppendGeometry(
		const VMeshData& source, uint32_t first, uint32_t count, VMeshPageData& target, std::unordered_map<uint32_t, uint32_t>& remap) {
		for (uint32_t i = 0; i < count; ++i) {
			const uint32_t sourceIndex = source.Indices[first + i];
			const auto [entry, inserted] = remap.try_emplace(sourceIndex, static_cast<uint32_t>(target.Vertices.size()));
			if (inserted) {
				target.Vertices.push_back(source.Vertices[sourceIndex]);
			}
			target.Indices.push_back(entry->second);
		}
	}

	void BuildTransitionDirectory(const VMeshData& data, VMeshPagedMetadata& metadata) {
		struct Members {
			std::vector<uint32_t> Coarse {};
			std::vector<uint32_t> Fine {};
		};
		std::vector<Members> groups;
		const auto group = [&](uint32_t id) -> Members& {
			if (id >= data.Clusters.size()) {
				Fail("cook supplied an out-of-range transition ID");
			}
			if (id >= groups.size()) {
				groups.resize(size_t(id) + 1u);
			}
			return groups[id];
		};
		for (size_t id = 0; id < data.Clusters.size(); ++id) {
			const uint32_t clusterId = static_cast<uint32_t>(id);
			const auto& cluster = data.Clusters[id];
			if (cluster.GroupId != VMeshLeafGroupId) {
				group(cluster.GroupId).Coarse.push_back(clusterId);
			}
			if (data.ClusterConsumers[id] == UINT32_MAX) {
				metadata.Roots.push_back(clusterId);
			}
			else {
				group(data.ClusterConsumers[id]).Fine.push_back(clusterId);
			}
		}
		for (const auto& members : groups) {
			if (metadata.TransitionClusters.size() + uint64_t(members.Coarse.size()) + members.Fine.size() > UINT32_MAX) {
				Fail("too many transition members");
			}
			VMeshTransition transition;
			transition.CoarseFirst = static_cast<uint32_t>(metadata.TransitionClusters.size());
			transition.CoarseCount = static_cast<uint32_t>(members.Coarse.size());
			metadata.TransitionClusters.insert(metadata.TransitionClusters.end(), members.Coarse.begin(), members.Coarse.end());
			transition.FineFirst = static_cast<uint32_t>(metadata.TransitionClusters.size());
			transition.FineCount = static_cast<uint32_t>(members.Fine.size());
			metadata.TransitionClusters.insert(metadata.TransitionClusters.end(), members.Fine.begin(), members.Fine.end());
			metadata.Transitions.push_back(transition);
		}
	}

	std::vector<VMeshPageData> PackPages(const VMeshData& data, uint32_t pageBytes, VMeshPagedMetadata& metadata) {
		std::vector<VMeshPageData> pages;
		std::unordered_map<uint32_t, uint32_t> remap;
		metadata.Clusters = data.Clusters;
		metadata.ClusterPages.resize(data.Clusters.size());
		// Root pages never carry optional clusters. Within either tier, source cluster ID gives stable packing.
		for (const bool roots : { true, false }) {
			VMeshPageData* current = nullptr;
			for (size_t id = 0; id < data.Clusters.size(); ++id) {
				const auto& cluster = data.Clusters[id];
				if (((cluster.Flags & VMESH_CLUSTER_ROOT) != 0u) != roots) {
					continue;
				}
				// Bound additions assuming no vertex reuse. A single cluster exceeding the target has its own page.
				const auto maxAddition = PageSize(cluster.IndexCount, cluster.IndexCount);
				if (!current
					|| current->Vertices.size() * sizeof(VMeshVertex) + current->Indices.size() * sizeof(uint32_t) + maxAddition > pageBytes) {
					pages.emplace_back();
					current = &pages.back();
					remap.clear();
				}
				metadata.ClusterPages[id] = static_cast<uint32_t>(pages.size() - 1u);
				metadata.Clusters[id].IndexOffset = static_cast<uint32_t>(current->Indices.size());
				AppendGeometry(data, cluster.IndexOffset, cluster.IndexCount, *current, remap);
			}
		}
		return pages;
	}

	void PackFallback(const VMeshData& data, VMeshData& fallback) {
		std::unordered_map<uint32_t, uint32_t> remap;
		VMeshPageData geometry;
		fallback.Header = data.Header;
		for (const auto& sourceRange : data.FallbackRanges) {
			if (geometry.Indices.size() + uint64_t(sourceRange.IndexCount) > UINT32_MAX) {
				Fail("compact fallback exceeds 32-bit addressing");
			}
			auto range = sourceRange;
			range.IndexOffset = static_cast<uint32_t>(geometry.Indices.size());
			AppendGeometry(data, sourceRange.IndexOffset, sourceRange.IndexCount, geometry, remap);
			fallback.FallbackRanges.push_back(range);
		}
		fallback.Vertices = std::move(geometry.Vertices);
		fallback.Indices = std::move(geometry.Indices);
	}

	void WriteVMeshBytes(const std::string& nativePath, std::span<const uint8_t> bytes) {
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
}

std::vector<uint8_t> WriteVMeshPaged(const VMeshData& data, uint64_t cookKey, uint32_t pageBytes) {
	if (data.Clusters.size() > UINT32_MAX || data.Vertices.size() > UINT32_MAX || data.Indices.size() > UINT32_MAX
		|| data.ClusterConsumers.size() != data.Clusters.size() || data.FallbackRanges.empty() || !pageBytes || pageBytes > VMeshMaxPageBytes) {
		Fail("paged cook requires explicit DAG consumers, fallback ranges, valid counts, and a bounded page size");
	}
	ValidateVMesh(data);
	VMeshPagedMetadata metadata;
	auto& header = metadata.Header;
	header.CookKey = cookKey;
	header.Asset = data.Header;
	PackFallback(data, metadata.FallbackData);
	ValidateVertices(metadata.FallbackData.Vertices);
	ValidateIndices(metadata.FallbackData.Indices, metadata.FallbackData.Vertices.size());
	auto pages = PackPages(data, pageBytes, metadata);
	BuildTransitionDirectory(data, metadata);

	uint64_t cursor = sizeof(header);
	header.Asset.Vertices = MakeSection<VMeshVertex>(cursor, metadata.FallbackData.Vertices.size());
	header.Asset.Indices = MakeSection<uint32_t>(cursor, metadata.FallbackData.Indices.size());
	header.Asset.Clusters = MakeSection<VMeshCluster>(cursor, metadata.Clusters.size());
	header.Asset.FallbackRanges = MakeSection<VMeshFallbackRange>(cursor, metadata.FallbackData.FallbackRanges.size());
	header.ClusterPages = MakeSection<uint32_t>(cursor, metadata.ClusterPages.size());
	header.Transitions = MakeSection<VMeshTransition>(cursor, metadata.Transitions.size());
	header.TransitionClusters = MakeSection<uint32_t>(cursor, metadata.TransitionClusters.size());
	header.Roots = MakeSection<uint32_t>(cursor, metadata.Roots.size());
	header.Pages = MakeSection<VMeshPageEntry>(cursor, pages.size());
	header.MetadataSize = cursor;
	for (const auto& page : pages) {
		VMeshPageEntry entry;
		entry.Offset = AlignUp(cursor, VMeshSectionAlignment);
		entry.VertexCount = static_cast<uint32_t>(page.Vertices.size());
		entry.IndexCount = static_cast<uint32_t>(page.Indices.size());
		entry.Size = PageSize(entry.VertexCount, entry.IndexCount);
		const auto vertexHash = HashVMeshBytes(page.Vertices.data(), page.Vertices.size() * sizeof(VMeshVertex));
		entry.Checksum = HashVMeshBytes(page.Indices.data(), page.Indices.size() * sizeof(uint32_t), vertexHash);
		metadata.Pages.push_back(entry);
		cursor = entry.Offset + entry.Size;
	}
	header.FileSize = cursor;
	CheckPagedSections(header);
	ValidateTransitions(metadata);
	ValidatePageDirectory(metadata);
	if (cursor > std::numeric_limits<size_t>::max() || header.MetadataSize > 512ull * 1024u * 1024u) {
		Fail("paged cook exceeds metadata or address-space limits");
	}
	std::vector<uint8_t> bytes(sizeof(header));
	bytes.reserve(static_cast<size_t>(cursor));
	AppendSection(bytes, header.Asset.Vertices, metadata.FallbackData.Vertices);
	AppendSection(bytes, header.Asset.Indices, metadata.FallbackData.Indices);
	AppendSection(bytes, header.Asset.Clusters, metadata.Clusters);
	AppendSection(bytes, header.Asset.FallbackRanges, metadata.FallbackData.FallbackRanges);
	AppendSection(bytes, header.ClusterPages, metadata.ClusterPages);
	AppendSection(bytes, header.Transitions, metadata.Transitions);
	AppendSection(bytes, header.TransitionClusters, metadata.TransitionClusters);
	AppendSection(bytes, header.Roots, metadata.Roots);
	AppendSection(bytes, header.Pages, metadata.Pages);
	header.MetadataChecksum = MetadataChecksum(header, bytes);
	std::memcpy(bytes.data(), &header, sizeof(header));
	for (size_t id = 0; id < pages.size(); ++id) {
		const auto& page = pages[id];
		const size_t start = static_cast<size_t>(metadata.Pages[id].Offset);
		const size_t vertexBytes = page.Vertices.size() * sizeof(VMeshVertex);
		bytes.resize(start + static_cast<size_t>(metadata.Pages[id].Size), 0u);
		std::memcpy(bytes.data() + start, page.Vertices.data(), vertexBytes);
		std::memcpy(bytes.data() + start + vertexBytes, page.Indices.data(), page.Indices.size() * sizeof(uint32_t));
	}
	return bytes;
}

void WriteVMeshPagedFile(const std::string& nativePath, const VMeshData& data, uint64_t cookKey, uint32_t pageBytes) {
	WriteVMeshBytes(nativePath, WriteVMeshPaged(data, cookKey, pageBytes));
}

VMeshData ReadVMesh(const uint8_t* bytes, size_t size) {
	if (bytes && size >= 2 * sizeof(uint32_t)) {
		std::array<uint32_t, 2> identity {};
		std::memcpy(identity.data(), bytes, sizeof(identity));
		if (identity[0] == VMeshMagic && identity[1] == VMeshPagedVersion) {
			return MaterializePaged(std::span<const uint8_t>(bytes, size));
		}
	}
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
