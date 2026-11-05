#pragma once
#include "renderGraphCompiler.h"
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>

namespace IKIGAI::RENDER::GRAPH {
struct UseDescriptor {
	std::string Resource;
	uint32_t Version = 0;
	Access Mode = Access::SAMPLED;

	template <class Context> static constexpr auto serde(Context& context, UseDescriptor& value) {
		serde::serde_struct(context, value)
			.field(&UseDescriptor::Resource, "Resource")
			.field(&UseDescriptor::Version, "Version")
			.field(&UseDescriptor::Mode, "Access");
	}
};

struct Descriptor {
	bool Enabled = false;
	bool Aliasing = false;
	std::vector<std::string> Imports;
	std::vector<std::string> Exports;

	template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
		using serde::attribute::default_;
		serde::serde_struct(context, value)
			.field(&Descriptor::Enabled, "Enabled", default_ { false })
			.field(&Descriptor::Aliasing, "Aliasing", default_ { false })
			.field(&Descriptor::Imports, "Imports", default_ { std::vector<std::string> {} })
			.field(&Descriptor::Exports, "Exports", default_ { std::vector<std::string> {} });
	}
};
}
