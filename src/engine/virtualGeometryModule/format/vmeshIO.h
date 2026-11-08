#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "virtualGeometryModule/vmeshFormat.h"

// Reading, writing and validation of cooked virtual meshes (.vmesh). Available on every platform; only the
// cooker (virtualGeometryModule/cook) depends on meshoptimizer.
namespace IKIGAI::VG {
inline constexpr uint32_t VMeshLeafGroupId = 0xFFFFFFFFu;
inline constexpr uint64_t VMeshSectionAlignment = 16u;
inline constexpr uint32_t VMeshDefaultPageBytes = 64u * 1024u;
inline constexpr uint32_t VMeshMaxPageBytes = 16u * 1024u * 1024u;

// In-memory form of a .vmesh file. Header.Vertices/Indices/Clusters/FallbackRanges describe the file layout:
// ReadVMesh fills them from the file, WriteVMesh recomputes them from the vectors.
struct VMeshData {
	VMeshHeader Header {};
	std::vector<VMeshVertex> Vertices {};
	std::vector<uint32_t> Indices {};
	std::vector<VMeshCluster> Clusters {};
	std::vector<VMeshFallbackRange> FallbackRanges {};
	// Cook-only input for v2 serialization. UINT32_MAX identifies a root.
	std::vector<uint32_t> ClusterConsumers {};
};

struct VMeshPagedMetadata {
	VMeshPagedHeader Header {};
	std::vector<VMeshCluster> Clusters {};
	std::vector<uint32_t> ClusterPages {};
	std::vector<VMeshTransition> Transitions {};
	std::vector<uint32_t> TransitionClusters {};
	std::vector<uint32_t> Roots {};
	std::vector<VMeshPageEntry> Pages {};
	VMeshData FallbackData {};
};

struct VMeshPageData {
	std::vector<VMeshVertex> Vertices {};
	std::vector<uint32_t> Indices {};
};

VMeshPagedHeader ReadVMeshPagedHeader(std::span<const uint8_t> bytes, uint64_t fileSize);
VMeshPagedMetadata ReadVMeshPagedMetadata(std::span<const uint8_t> bytes, uint64_t fileSize);
VMeshPageData ReadVMeshPage(const VMeshPagedMetadata& metadata, uint32_t pageId, std::span<const uint8_t> bytes);
// Pages must have been validated with ReadVMeshPage and supplied in directory order.
VMeshData MaterializeVMeshPages(const VMeshPagedMetadata& metadata, std::span<const VMeshPageData> pages);
VMeshData MaterializeVMeshPages(const VMeshPagedMetadata& metadata, const std::function<VMeshPageData(uint32_t)>& readPage);
std::vector<uint8_t> WriteVMeshPaged(const VMeshData& data, uint64_t cookKey, uint32_t pageBytes = VMeshDefaultPageBytes);
void WriteVMeshPagedFile(const std::string& nativePath, const VMeshData& data, uint64_t cookKey, uint32_t pageBytes = VMeshDefaultPageBytes);

// Every failure of ValidateVMesh/ReadVMesh/WriteVMesh is reported with this exception; the message names the
// failed check and the offending element.
class VMeshFormatError : public std::runtime_error {
public:
	explicit VMeshFormatError(const std::string& message);
};

// Checks the full contract of vmeshFormat.h on decoded data: header identity, material slot and LOD level
// ranges, index ranges, IndexCount % 3, cluster size, finite floats, error monotonicity (ParentError >= Error,
// roots use VMeshRootError) and group consistency (clusters with the same GroupId share Error and LodBounds).
// Throws VMeshFormatError. Header section descriptors are not checked here (see ReadVMesh).
void ValidateVMesh(const VMeshData& data);

// Recomputes the header section table (16-byte aligned, in file order) for the current vector sizes.
void UpdateVMeshSections(VMeshData& data);

// Validates data and serializes it into a byte buffer (section table recomputed). Throws VMeshFormatError.
std::vector<uint8_t> WriteVMesh(const VMeshData& data);
// WriteVMesh plus a binary write to a native filesystem path (parent directories are created).
void WriteVMeshFile(const std::string& nativePath, const VMeshData& data);

// Parses and validates a .vmesh image: magic, version, vertex layout, section strides, sizes, alignment, bounds
// within the buffer and overlap, then ValidateVMesh on the decoded data. Never reads outside [bytes, bytes+size).
// Throws VMeshFormatError on any violation.
VMeshData ReadVMesh(const uint8_t* bytes, size_t size);
VMeshData ReadVMesh(const std::vector<uint8_t>& bytes);
// Reads a whole native file and calls ReadVMesh.
VMeshData ReadVMeshFile(const std::string& nativePath);

// 64-bit FNV-1a, used for VMeshHeader::SourceHash and SettingsHash.
uint64_t HashVMeshBytes(const void* bytes, size_t size, uint64_t seed = 0xCBF29CE484222325ull);

// Summary used by the cooker log and tests.
struct VMeshStatistics {
	uint32_t VertexCount = 0;
	uint32_t IndexCount = 0;
	uint32_t ClusterCount = 0;
	uint32_t LeafClusterCount = 0;
	uint32_t RootClusterCount = 0;
	uint32_t LeafTriangleCount = 0;
	uint32_t FallbackTriangleCount = 0;
	uint32_t LodLevelCount = 0;
	uint32_t GroupCount = 0;
	std::vector<uint32_t> ClustersPerLevel {};
	std::vector<uint32_t> TrianglesPerLevel {};
};
VMeshStatistics ComputeVMeshStatistics(const VMeshData& data);
}
