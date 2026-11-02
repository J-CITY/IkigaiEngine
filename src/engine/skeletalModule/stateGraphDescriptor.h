#pragma once

#include "stateGraphIr.h"
#include <map>
#include <nlohmann/json.hpp>
#include <serdepp/serde.hpp>
#include <string>
#include <vector>

namespace IKIGAI::SKELETON {

struct ClipEventDescriptor {
	std::string Name;
	float Time = 0.f;
	template <class C> static auto serde(C& context, ClipEventDescriptor& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value).field(&ClipEventDescriptor::Name, "Name").field(&ClipEventDescriptor::Time, "Time", default_ { 0.f });
	}
};

struct ClipDescriptor {
	std::string Path;
	bool Loop = true;
	float PhaseOffset = 0.f;
	std::string RootMode = "in-place";
	std::string RootMotion = "in-pose";
	bool RemoveYaw = false;
	std::vector<ClipEventDescriptor> Events;
	template <class C> static auto serde(C& context, ClipDescriptor& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&ClipDescriptor::Path, "Path")
			.field(&ClipDescriptor::Loop, "Loop", default_ { true })
			.field(&ClipDescriptor::PhaseOffset, "PhaseOffset", default_ { 0.f })
			.field(&ClipDescriptor::RootMode, "RootMode", default_ { std::string { "in-place" } })
			.field(&ClipDescriptor::RootMotion, "RootMotion", default_ { std::string { "in-pose" } })
			.field(&ClipDescriptor::RemoveYaw, "RemoveYaw", default_ { false })
			.field(&ClipDescriptor::Events, "Events", default_ { std::vector<ClipEventDescriptor> {} });
	}
};

struct LayerDescriptor {
	std::string Mode = "override";
	float Weight = 1.f;
	std::vector<std::string> MaskJoints;
	uint32_t Mask = ~0u;
	nlohmann::json Node;
	template <class C> static auto serde(C& context, LayerDescriptor& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&LayerDescriptor::Mode, "Mode", default_ { std::string { "override" } })
			.field(&LayerDescriptor::Weight, "Weight", default_ { 1.f })
			.field(&LayerDescriptor::MaskJoints, "MaskJoints", default_ { std::vector<std::string> {} })
			.field(&LayerDescriptor::Mask, "Mask", default_ { ~0u })
			.field(&LayerDescriptor::Node, "Node");
	}
};

ClipRootMode parseClipRootMode(const std::string& mode);
RootMotionPolicy parseRootMotionPolicy(const std::string& policy);
AnimationLayerMode parseLayerMode(const std::string& mode);
uint32_t compileBoneMask(const Skeleton& skeleton, uint32_t explicitMask, const std::vector<std::string>& joints);

} // namespace IKIGAI::SKELETON
