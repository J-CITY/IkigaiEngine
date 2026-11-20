#pragma once

#include <cstdint>
#include <vector>

// Topology helpers of the cluster LOD builder (cooker only). Vertex ids are canonical position ids (the result of
// meshopt_generatePositionRemap), so edges and triangles are compared by position, not by attribute wedge.
namespace IKIGAI::VG::COOK {
// Edge (by canonical position ids) and its triangle use count.
struct EdgeUse {
	uint64_t Key = 0;
	uint32_t Count = 0;
};

uint64_t EdgeKey(uint32_t a, uint32_t b);
// Sorted list of the undirected edges of a triangle list with their use counts.
std::vector<EdgeUse> CountEdges(const std::vector<uint32_t>& indices, const std::vector<uint32_t>& remap);
uint32_t FindEdgeCount(const std::vector<EdgeUse>& edges, uint64_t key);
// Removes zero-volume fins (same three positions, opposite winding) that simplification created; position triples
// present in the input are kept.
void RemoveFinPairs(std::vector<uint32_t>& indices, const std::vector<uint32_t>& input, const std::vector<uint32_t>& remap);
// Topology check of a simplified group against its input (see the implementation for the rules). borderIds are the
// sorted canonical ids locked at the group border. On failure, lockIds receives the canonical ids to lock before a
// retry, or the single value 0xFFFFFFFF when no local remedy exists.
bool CheckTopology(const std::vector<uint32_t>& merged, const std::vector<uint32_t>& simplified, const std::vector<uint32_t>& remap,
	const std::vector<uint32_t>& borderIds, std::vector<uint32_t>& lockIds);
}
