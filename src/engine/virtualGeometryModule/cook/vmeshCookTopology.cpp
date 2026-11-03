#include "vmeshCookTopology.h"

#if defined(IKIGAI_VG_COOKER)

#include <algorithm>
#include <array>

#include <meshoptimizer.h>

namespace IKIGAI::VG::COOK {

uint64_t EdgeKey(uint32_t a, uint32_t b) {
	return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
}

std::vector<EdgeUse> CountEdges(const std::vector<uint32_t>& indices, const std::vector<uint32_t>& remap) {
	std::vector<uint64_t> keys;
	keys.reserve(indices.size());
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		for (int e = 0; e < 3; ++e) {
			keys.push_back(EdgeKey(remap[indices[i + e]], remap[indices[i + (e + 1) % 3]]));
		}
	}
	std::sort(keys.begin(), keys.end());
	std::vector<EdgeUse> edges;
	for (size_t i = 0; i < keys.size();) {
		size_t j = i;
		while (j < keys.size() && keys[j] == keys[i]) {
			++j;
		}
		edges.push_back(EdgeUse { keys[i], uint32_t(j - i) });
		i = j;
	}
	return edges;
}

uint32_t FindEdgeCount(const std::vector<EdgeUse>& edges, uint64_t key) {
	const auto found = std::lower_bound(edges.begin(), edges.end(), key, [](const EdgeUse& edge, uint64_t value) { return edge.Key < value; });
	return found != edges.end() && found->Key == key ? found->Count : 0u;
}

// Removes pairs of triangles that cover the same three positions with opposite winding (zero-volume fins left
// by edge collapses). Position triples that already exist in the input are kept (source geometry may contain
// double-sided duplicates; removing them would open the surface).
void RemoveFinPairs(std::vector<uint32_t>& indices, const std::vector<uint32_t>& input, const std::vector<uint32_t>& remap) {
	std::vector<std::array<uint32_t, 3>> inputTriangles;
	inputTriangles.reserve(input.size() / 3u);
	for (size_t i = 0; i + 2 < input.size(); i += 3) {
		std::array<uint32_t, 3> key { remap[input[i]], remap[input[i + 1]], remap[input[i + 2]] };
		std::sort(key.begin(), key.end());
		inputTriangles.push_back(key);
	}
	std::sort(inputTriangles.begin(), inputTriangles.end());
	struct TriangleKey {
		uint32_t Sorted[3] = { 0, 0, 0 };
		bool Even = true; // winding parity relative to the sorted order
		size_t Triangle = 0;
	};
	std::vector<TriangleKey> keys(indices.size() / 3u);
	for (size_t t = 0; t < keys.size(); ++t) {
		uint32_t v[3] = { remap[indices[t * 3 + 0]], remap[indices[t * 3 + 1]], remap[indices[t * 3 + 2]] };
		// Rotate the smallest id first; the remaining order gives the winding.
		int first = 0;
		if (v[1] < v[first]) {
			first = 1;
		}
		if (v[2] < v[first]) {
			first = 2;
		}
		const uint32_t a = v[first];
		const uint32_t b = v[(first + 1) % 3];
		const uint32_t c = v[(first + 2) % 3];
		keys[t].Sorted[0] = a;
		keys[t].Sorted[1] = std::min(b, c);
		keys[t].Sorted[2] = std::max(b, c);
		keys[t].Even = b < c;
		keys[t].Triangle = t;
	}
	std::sort(keys.begin(), keys.end(),
		[](const TriangleKey& x, const TriangleKey& y) { return std::lexicographical_compare(x.Sorted, x.Sorted + 3, y.Sorted, y.Sorted + 3); });
	std::vector<uint8_t> removed(keys.size(), 0u);
	for (size_t i = 0; i < keys.size();) {
		size_t j = i;
		while (j < keys.size() && std::equal(keys[j].Sorted, keys[j].Sorted + 3, keys[i].Sorted)) {
			++j;
		}
		const std::array<uint32_t, 3> triple { keys[i].Sorted[0], keys[i].Sorted[1], keys[i].Sorted[2] };
		if (std::binary_search(inputTriangles.begin(), inputTriangles.end(), triple)) {
			i = j;
			continue;
		}
		// Cancel opposite windings pairwise.
		std::vector<size_t> even;
		std::vector<size_t> odd;
		for (size_t k = i; k < j; ++k) {
			(keys[k].Even ? even : odd).push_back(keys[k].Triangle);
		}
		const size_t pairs = std::min(even.size(), odd.size());
		for (size_t k = 0; k < pairs; ++k) {
			removed[even[k]] = 1u;
			removed[odd[k]] = 1u;
		}
		i = j;
	}
	size_t write = 0;
	for (size_t t = 0; t < keys.size(); ++t) {
		if (!removed[t]) {
			indices[write++] = indices[t * 3 + 0];
			indices[write++] = indices[t * 3 + 1];
			indices[write++] = indices[t * 3 + 2];
		}
	}
	indices.resize(write);
}

// Checks that a simplified group keeps the surface topology the DAG relies on:
//  - no edge is used by more than two triangles (pinches, fins),
//  - no new open edge except along open borders of the input,
//  - every locked border edge of the input is still used exactly once,
//  - no new edge connects two locked border vertices (the neighbouring group, a frozen root group or a later
//    level could build the same edge on the other side of the border, which would pinch it).
// On failure, lockIds receives canonical vertex ids whose locking avoids the failure on a retry.
bool CheckTopology(const std::vector<uint32_t>& merged, const std::vector<uint32_t>& simplified, const std::vector<uint32_t>& remap,
	const std::vector<uint32_t>& borderIds, std::vector<uint32_t>& lockIds) {
	lockIds.clear();
	const std::vector<EdgeUse> before = CountEdges(merged, remap);
	const std::vector<EdgeUse> after = CountEdges(simplified, remap);
	std::vector<uint32_t> openVertices;
	for (const EdgeUse& edge : before) {
		if (edge.Count == 1u) {
			openVertices.push_back(uint32_t(edge.Key >> 32));
			openVertices.push_back(uint32_t(edge.Key & 0xFFFFFFFFu));
		}
	}
	std::sort(openVertices.begin(), openVertices.end());
	openVertices.erase(std::unique(openVertices.begin(), openVertices.end()), openVertices.end());
	auto isOpen = [&](uint32_t vertex) { return std::binary_search(openVertices.begin(), openVertices.end(), vertex); };
	auto isBorder = [&](uint32_t vertex) { return std::binary_search(borderIds.begin(), borderIds.end(), vertex); };
	bool lostBorder = false;
	for (const EdgeUse& edge : after) {
		const uint32_t a = uint32_t(edge.Key >> 32);
		const uint32_t b = uint32_t(edge.Key & 0xFFFFFFFFu);
		const uint32_t usesBefore = FindEdgeCount(before, edge.Key);
		if (usesBefore > 2u) {
			continue; // the input is already non-manifold on this edge; nothing to preserve
		}
		// An edge may only become (or appear as) an open edge along an open border of the input: collapses along an open
		// border legitimately expose former interior edges there; anywhere else it is a hole.
		if (edge.Count > 2u || (edge.Count == 1u && (usesBefore != 1u && !(isOpen(a) && isOpen(b))))) {
			lockIds.push_back(a);
			lockIds.push_back(b);
		}
	}
	for (const EdgeUse& edge : before) {
		const uint32_t a = uint32_t(edge.Key >> 32);
		const uint32_t b = uint32_t(edge.Key & 0xFFFFFFFFu);
		if (edge.Count == 1u && isBorder(a) && isBorder(b) && FindEdgeCount(after, edge.Key) != 1u) {
			lostBorder = true;
		}
	}
	// New edges between two locked border vertices (chords): another group, a frozen root or a later level can
	// produce the same edge on the other side of the border, which would pinch it.
	std::vector<uint32_t> earVertices;
	for (const EdgeUse& edge : after) {
		const uint32_t a = uint32_t(edge.Key >> 32);
		const uint32_t b = uint32_t(edge.Key & 0xFFFFFFFFu);
		if (isBorder(a) && isBorder(b) && FindEdgeCount(before, edge.Key) == 0u) {
			earVertices.push_back(a);
			earVertices.push_back(b);
		}
	}
	if (!earVertices.empty() || lostBorder) {
		// Lock the interior neighbours of the offending border vertices so they cannot collapse onto the border.
		std::sort(earVertices.begin(), earVertices.end());
		for (size_t i = 0; i + 2 < merged.size(); i += 3) {
			const uint32_t v[3] = { remap[merged[i]], remap[merged[i + 1]], remap[merged[i + 2]] };
			const bool touches = std::binary_search(earVertices.begin(), earVertices.end(), v[0])
				|| std::binary_search(earVertices.begin(), earVertices.end(), v[1])
				|| std::binary_search(earVertices.begin(), earVertices.end(), v[2]);
			if (!touches) {
				continue;
			}
			for (uint32_t vertex : v) {
				if (!isBorder(vertex)) {
					lockIds.push_back(vertex);
				}
			}
		}
		if (lostBorder || lockIds.empty()) {
			lockIds.assign(1u, ~0u); // no local remedy: relax the target instead
		}
	}
	std::sort(lockIds.begin(), lockIds.end());
	lockIds.erase(std::unique(lockIds.begin(), lockIds.end()), lockIds.end());
	return lockIds.empty();
}
}

#endif
