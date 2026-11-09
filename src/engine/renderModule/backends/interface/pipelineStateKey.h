#pragma once

#include "vertexLayout.h"

#include <array>
#include <optional>

namespace IKIGAI::RENDER {
// Locations and bindings are limited to 16 by VertexLayout::validate.
// Indexing by slot makes declaration order irrelevant to the native pipeline.
struct VertexPipelineLayoutKey {
	explicit VertexPipelineLayoutKey(const VertexLayout& layout) {
		for (const auto& stream : layout.streams) {
			streams.at(stream.binding) = stream;
		}
		for (const auto& attribute : layout.attributes) {
			attributes.at(attribute.location) = attribute;
		}
	}

	auto operator<=>(const VertexPipelineLayoutKey&) const = default;

private:
	std::array<std::optional<VertexStreamLayout>, 16> streams {};
	std::array<std::optional<VertexInputAttribute>, 16> attributes {};
};
}
