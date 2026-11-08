#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace IKIGAI::DESTRUCTION {
using SupportId = std::uint64_t;
struct SupportBond {
	SupportId pieceA = 0;
	SupportId pieceB = 0;
};
struct SupportAnchor {
	SupportId id = 0;
	bool enabled = true;
};
struct SupportAttachment {
	SupportId piece = 0;
	SupportId anchor = 0;
};
// Borrowed synchronous input. IDs are resolved identities, not body handles or array indices.
// Piece and anchor IDs occupy separate namespaces; equal numbers across them are valid.
// Bonds describe intact material connectivity, never transient physics contacts.
struct SupportGraphView {
	std::span<const SupportId> pieces {};
	std::span<const SupportBond> bonds {};
	std::span<const SupportAnchor> anchors {};
	std::span<const SupportAttachment> attachments {};
};
struct SupportIsland {
	std::vector<SupportId> pieces {};
	std::vector<SupportId> activeAnchors {};
	bool supported = false;
};
struct SupportAnalysis {
	std::vector<SupportIsland> islands {};
};
struct SupportGraphLimits {
	static constexpr std::size_t MaximumPieces = 4096;
	static constexpr std::size_t MaximumBonds = 16384;
	static constexpr std::size_t MaximumAnchors = 4096;
	static constexpr std::size_t MaximumAttachments = 16384;
};
// Stable order: islands by smallest piece ID, members and anchors ascending.
// Sharing an anchor does not join material islands. Disabled anchors give no support.
// Invalid/duplicate IDs, edges or references throw invalid_argument; limits throw length_error.
// Pure analysis: no world mutations and no usable partial result on failure.
SupportAnalysis analyzeSupportGraph(const SupportGraphView& graph);
}
