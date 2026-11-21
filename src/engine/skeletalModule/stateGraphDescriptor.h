#pragma once

#include "stateGraphIr.h"
#include "utilsModule/serialization/jsonValueAdaptor.h"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
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
	std::optional<std::string> PhaseReference;
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
			.field(&ClipDescriptor::PhaseReference, "PhaseReference")
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
	std::optional<nlohmann::json> WeightExpression;
	template <class C> static auto serde(C& context, LayerDescriptor& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&LayerDescriptor::Mode, "Mode", default_ { std::string { "override" } })
			.field(&LayerDescriptor::Weight, "Weight", default_ { 1.f })
			.field(&LayerDescriptor::MaskJoints, "MaskJoints", default_ { std::vector<std::string> {} })
			.field(&LayerDescriptor::Mask, "Mask", default_ { ~0u })
			.field(&LayerDescriptor::Node, "Node")
			.field(&LayerDescriptor::WeightExpression, "WeightExpression");
	}
};

// The v2 asset syntax allows scalar/array expressions and several node shapes.
// These fields are decoded by the resource loader before it publishes the compiled graph.
struct StateGraphParameterDescriptor {
	std::string Type;
	std::optional<nlohmann::json> Default;
	template <class C> static void serde(C& context, StateGraphParameterDescriptor& value) {
		using Self = StateGraphParameterDescriptor;
		serde::serde_struct(context, value).field(&Self::Type, "Type").field(&Self::Default, "Default");
	}
};

struct StateGraphTransitionDescriptor {
	std::string Target;
	nlohmann::json When = true;
	std::vector<std::string> Consume;
	int Priority = 0;
	bool Interrupt = false;
	bool Force = false;
	float BlendTime = .15f;
	std::string displayText;

	template <class C> static void serde(C& context, StateGraphTransitionDescriptor& value) {
		using Self = StateGraphTransitionDescriptor;
		using namespace serde::attribute;
		if constexpr (!C::is_serialize) {
			value.displayText = context.adaptor.dump();
		}
		serde::serde_struct(context, value)
			.field(&Self::Target, "Target")
			.field(&Self::When, "When", default_ { nlohmann::json(true) })
			.field(&Self::Consume, "Consume", default_ { std::vector<std::string> {} })
			.field(&Self::Priority, "Priority", default_ { 0 })
			.field(&Self::Interrupt, "Interrupt", default_ { false })
			.field(&Self::Force, "Force", default_ { false })
			.field(&Self::BlendTime, "BlendTime", default_ { .15f });
	}
};

struct StateGraphStateDescriptor {
	nlohmann::json Node;
	std::string Clock;
	std::optional<nlohmann::json> Rate;
	std::optional<nlohmann::json> EntryTime;
	std::optional<nlohmann::json> ExternalTime;
	std::vector<StateGraphTransitionDescriptor> Transitions;
	std::string SyncGroup;
	bool ApplyPoseOffset = true;
	bool PreserveTime = false;
	bool LockTransitions = false;

	template <class C> static void serde(C& context, StateGraphStateDescriptor& value) {
		using Self = StateGraphStateDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Node, "Node")
			.field(&Self::Clock, "Clock")
			.field(&Self::Rate, "Rate")
			.field(&Self::EntryTime, "EntryTime")
			.field(&Self::ExternalTime, "ExternalTime")
			.field(&Self::Transitions, "Transitions", default_ { std::vector<StateGraphTransitionDescriptor> {} })
			.field(&Self::SyncGroup, "SyncGroup", default_ { std::string {} })
			.field(&Self::ApplyPoseOffset, "ApplyPoseOffset", default_ { true })
			.field(&Self::PreserveTime, "PreserveTime", default_ { false })
			.field(&Self::LockTransitions, "LockTransitions", default_ { false });
	}
};

struct StateGraphRigDescriptor {
	std::string Root = "Hips";
	float UnitsToMeters = .01f;
	float RootHeight = 90.f;
	std::vector<std::string> Feet;
	std::vector<std::string> Hands;
	std::vector<std::string> PhaseJoints { "LeftFoot", "RightFoot" };

	template <class C> static void serde(C& context, StateGraphRigDescriptor& value) {
		using Self = StateGraphRigDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Root, "Root", default_ { std::string("Hips") })
			.field(&Self::UnitsToMeters, "UnitsToMeters", default_ { .01f })
			.field(&Self::RootHeight, "RootHeight", default_ { 90.f })
			.field(&Self::Feet, "Feet", default_ { std::vector<std::string> {} })
			.field(&Self::Hands, "Hands", default_ { std::vector<std::string> {} })
			.field(&Self::PhaseJoints, "PhaseJoints", default_ { std::vector<std::string> { "LeftFoot", "RightFoot" } });
	}
};

struct StateGraphBindingDescriptor {
	nlohmann::json Expression;
	float Smoothing = 0.f;

	template <class C> static void serde(C& context, StateGraphBindingDescriptor& value) {
		using Self = StateGraphBindingDescriptor;
		using namespace serde::attribute;
		if constexpr (!C::is_serialize) {
			if (!context.adaptor.is_object()) {
				value.Expression = context.adaptor;
				return;
			}
		}
		serde::serde_struct(context, value).field(&Self::Expression, "Expression").field(&Self::Smoothing, "Smoothing", default_ { 0.f });
	}
};

struct StateGraphDescriptor {
	std::string SkeletonPath;
	std::string InitialState;
	std::optional<StateGraphRigDescriptor> Rig;
	std::map<std::string, StateGraphParameterDescriptor> Parameters;
	std::map<std::string, StateGraphBindingDescriptor> Bindings;
	std::map<std::string, ClipDescriptor> Clips;
	std::map<std::string, StateGraphStateDescriptor> States;
	std::vector<LayerDescriptor> Layers;
	std::vector<StateGraphTransitionDescriptor> AnyState;
	std::vector<std::string> Dependencies;

	template <class C> static void serde(C& context, StateGraphDescriptor& value) {
		using Self = StateGraphDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::SkeletonPath, "SkeletonPath")
			.field(&Self::InitialState, "InitialState")
			.field(&Self::Rig, "Rig")
			.field(&Self::Parameters, "Parameters")
			.field(&Self::Bindings, "Bindings", default_ { std::map<std::string, StateGraphBindingDescriptor> {} })
			.field(&Self::Clips, "Clips")
			.field(&Self::States, "States")
			.field(&Self::Layers, "Layers", default_ { std::vector<LayerDescriptor> {} })
			.field(&Self::AnyState, "AnyState", default_ { std::vector<StateGraphTransitionDescriptor> {} })
			.field(&Self::Dependencies, "Dependencies", default_ { std::vector<std::string> {} });
	}
};

ClipRootMode parseClipRootMode(const std::string& mode);
RootMotionPolicy parseRootMotionPolicy(const std::string& policy);
AnimationLayerMode parseLayerMode(const std::string& mode);
uint32_t compileBoneMask(const Skeleton& skeleton, uint32_t explicitMask, const std::vector<std::string>& joints);

} // namespace IKIGAI::SKELETON
