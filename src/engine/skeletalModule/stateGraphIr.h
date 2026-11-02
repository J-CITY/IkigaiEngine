#pragma once

#include "mathModule/math.h"
#include "utilsModule/expressions/expression.h"
#include "utilsModule/expressions/stateMachine.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace IKIGAI::SKELETON {
class Animation;
class Skeleton;

enum class ClipRootMode : uint8_t { InPlace, Stairs, Flat, Traversal, Authored };

enum class RootMotionPolicy : uint8_t {
	// Root translation stays in the sampled pose (after compile-time RootMode baking).
	InPose,
	// Per-tick root delta is exposed via StateGraphRuntime::rootMotion(); root joint is held at bind pose in output.
	Extract,
};

enum class AnimationLayerMode : uint8_t { Override, Additive };

struct ClipTimelineEvent {
	float timeSeconds = 0.f;
	std::string name;
};

struct CompiledClipMeta {
	std::string name;
	bool loop = true;
	float phaseOffset = 0.f;
	ClipRootMode rootMode = ClipRootMode::InPlace;
	bool removeYaw = false;
	RootMotionPolicy rootMotion = RootMotionPolicy::InPose;
	std::vector<ClipTimelineEvent> events;
};

struct ClipNode {
	uint16_t clipIndex = 0;
};

struct BlendNode {
	uint16_t childA = 0;
	uint16_t childB = 0;
	uint16_t weightProgram = 0;
};

struct SelectNode {
	uint16_t conditionProgram = 0;
	uint16_t whenTrue = 0;
	uint16_t whenFalse = 0;
};

struct BlendspacePoint {
	float value = 0.f;
	uint16_t nodeIndex = 0;
};

struct Blendspace1DNode {
	uint16_t parameterProgram = 0;
	std::vector<BlendspacePoint> points;
};

using StateGraphNode = std::variant<ClipNode, BlendNode, SelectNode, Blendspace1DNode>;

struct CompiledState {
	std::string name;
	uint16_t nodeIndex = 0;
	uint16_t clockClipIndex = 0;
	std::optional<uint16_t> rateProgram;
	std::optional<uint16_t> entryTimeProgram;
	std::optional<uint16_t> externalTimeProgram;
	bool applyPoseOffset = true;
	bool preserveTime = false;
	bool lockTransitions = false;
	std::string syncGroup;
};

struct CompiledLayer {
	AnimationLayerMode mode = AnimationLayerMode::Override;
	uint16_t nodeIndex = 0;
	std::optional<uint16_t> weightProgram;
	float defaultWeight = 1.f;
	uint32_t boneMask = ~0u;
};

struct StateGraphRig {
	int rootJoint = -1;
	float unitsToMeters = .01f;
	float rootHeight = 90.f;
};

struct StateGraphIR {
	std::vector<StateGraphNode> nodes;
	std::vector<CompiledClipMeta> clips;
	std::vector<CompiledState> states;
	std::vector<CompiledLayer> layers;
	StateGraphRig rig;
	size_t initialStateIndex = 0;
	std::map<std::string, size_t> stateByName;
	DATA::StateMachineDefinition machine;
};

struct RootMotionSample {
	MATH::Vector3f translationDeltaMeters {};
	MATH::QuaternionF rotationDelta { 0.f, 0.f, 0.f, 1.f };
	bool valid = false;
};

struct AnimationEventDispatch {
	std::string name;
	std::string clip;
	float timeSeconds = 0.f;
};

} // namespace IKIGAI::SKELETON
