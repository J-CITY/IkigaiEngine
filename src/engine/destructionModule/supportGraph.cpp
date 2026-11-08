#include "supportGraph.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace IKIGAI::DESTRUCTION {
namespace {
	std::size_t resolve(const std::vector<SupportId>& ids, SupportId id) {
		const auto found = std::lower_bound(ids.begin(), ids.end(), id);
		if (found == ids.end() || *found != id) {
			throw std::invalid_argument("Unknown support graph identity");
		}
		return static_cast<std::size_t>(found - ids.begin());
	}
	void validateIds(std::vector<SupportId>& ids) {
		std::sort(ids.begin(), ids.end());
		if ((!ids.empty() && ids.front() == 0) || std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
			throw std::invalid_argument("Support graph requires unique nonzero identities");
		}
	}
	template <class T> void validateEdges(std::vector<T>& edges) {
		std::sort(edges.begin(), edges.end());
		if (std::adjacent_find(edges.begin(), edges.end()) != edges.end()) {
			throw std::invalid_argument("Duplicate support graph edge");
		}
	}
}
SupportAnalysis analyzeSupportGraph(const SupportGraphView& graph) {
	if (graph.pieces.size() > SupportGraphLimits::MaximumPieces || graph.bonds.size() > SupportGraphLimits::MaximumBonds
		|| graph.anchors.size() > SupportGraphLimits::MaximumAnchors || graph.attachments.size() > SupportGraphLimits::MaximumAttachments) {
		throw std::length_error("Support graph budget exceeded");
	}
	std::vector<SupportId> pieces(graph.pieces.begin(), graph.pieces.end());
	validateIds(pieces);
	std::vector<SupportId> anchors {};
	anchors.reserve(graph.anchors.size());
	for (const auto& anchor : graph.anchors) {
		anchors.push_back(anchor.id);
	}
	validateIds(anchors);
	std::vector<bool> enabled(anchors.size(), false);
	for (const auto& anchor : graph.anchors) {
		enabled[resolve(anchors, anchor.id)] = anchor.enabled;
	}
	std::vector<std::pair<std::size_t, std::size_t>> bonds {};
	bonds.reserve(graph.bonds.size());
	for (const auto& bond : graph.bonds) {
		const auto a = resolve(pieces, bond.pieceA);
		const auto b = resolve(pieces, bond.pieceB);
		if (a == b) {
			throw std::invalid_argument("Self support bond");
		}
		bonds.emplace_back(std::min(a, b), std::max(a, b));
	}
	validateEdges(bonds);
	std::vector<std::pair<std::size_t, std::size_t>> attachments {};
	attachments.reserve(graph.attachments.size());
	for (const auto& attachment : graph.attachments) {
		attachments.emplace_back(resolve(pieces, attachment.piece), resolve(anchors, attachment.anchor));
	}
	validateEdges(attachments);
	std::vector<std::size_t> parent(pieces.size());
	std::iota(parent.begin(), parent.end(), 0);
	const auto root = [&](std::size_t node) {
		while (parent[node] != node) {
			parent[node] = parent[parent[node]];
			node = parent[node];
		}
		return node;
	};
	for (const auto& [a, b] : bonds) {
		const auto rootA = root(a);
		const auto rootB = root(b);
		parent[std::max(rootA, rootB)] = std::min(rootA, rootB);
	}
	SupportAnalysis result {};
	std::map<std::size_t, std::size_t> islandIndices {};
	for (std::size_t piece = 0; piece < pieces.size(); ++piece) {
		const auto [entry, inserted] = islandIndices.try_emplace(root(piece), result.islands.size());
		if (inserted) {
			result.islands.emplace_back();
		}
		result.islands[entry->second].pieces.push_back(pieces[piece]);
	}
	for (const auto& [piece, anchor] : attachments) {
		if (enabled[anchor]) {
			result.islands[islandIndices.at(root(piece))].activeAnchors.push_back(anchors[anchor]);
		}
	}
	for (auto& island : result.islands) {
		std::sort(island.activeAnchors.begin(), island.activeAnchors.end());
		island.activeAnchors.erase(std::unique(island.activeAnchors.begin(), island.activeAnchors.end()), island.activeAnchors.end());
		island.supported = !island.activeAnchors.empty();
	}
	return result;
}
}
