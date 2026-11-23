#include "skeletonStateGraphManager.h"
#include "utilsModule/expressions/expressionDescriptor.h"

#include "serviceManager.h"
#include "skeletalModule/characterAnimation.h"
#include "skeletalModule/stateGraphDescriptor.h"
#include "skeletonAnimationManager.h"
#include "skeletonManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <utility>

namespace IKIGAI::RESOURCES {
using namespace SKELETON;
namespace {
	using Json = nlohmann::json;
	void require(bool valid, const std::string& message) {
		if (!valid) {
			throw std::runtime_error("State graph: " + message);
		}
	}
	const std::map<std::string, std::pair<int, int>> operators
		= { { "+", { 2, 2 } }, { "-", { 2, 2 } }, { "*", { 2, 2 } }, { "/", { 2, 2 } }, { "abs", { 1, 1 } }, { "min", { 2, 2 } }, { "max", { 2, 2 } },
			  { "clamp", { 3, 3 } }, { ">", { 2, 2 } }, { "<", { 2, 2 } }, { ">=", { 2, 2 } }, { "<=", { 2, 2 } }, { "==", { 2, 2 } },
			  { "!=", { 2, 2 } }, { "and", { 2, 32 } }, { "or", { 2, 32 } }, { "not", { 1, 1 } }, { "if", { 3, 3 } } };
	UTILS::VarValue parameterValue(const StateGraphParameterDescriptor& parameter) {
		const auto& type = parameter.Type;
		if (parameter.Default) {
			const auto& value = *parameter.Default;
			require(type == "float" ? value.is_number()
					: type == "int" ? value.is_number_integer()
									: value.is_boolean(),
				"parameter default has wrong type");
		}
		if (type == "float") {
			return parameter.Default ? parameter.Default->get<float>() : 0.f;
		}
		if (type == "int") {
			return parameter.Default ? parameter.Default->get<int>() : 0;
		}
		if (type == "bool" || type == "trigger") {
			return parameter.Default ? parameter.Default->get<bool>() : false;
		}
		throw std::runtime_error("State graph: unknown parameter type " + type);
	}
	CompiledClipMeta readClipMeta(const std::string& name, const ClipDescriptor& clip) {
		CompiledClipMeta meta;
		meta.name = name;
		meta.loop = clip.Loop;
		meta.phaseOffset = clip.PhaseOffset;
		meta.rootMode = parseClipRootMode(clip.RootMode);
		meta.rootMotion = parseRootMotionPolicy(clip.RootMotion);
		meta.removeYaw = clip.RemoveYaw;
		for (const auto& event : clip.Events) {
			require(std::isfinite(event.Time), "invalid clip event time in " + name);
			meta.events.push_back({ event.Time, event.Name });
		}
		return meta;
	}
}

std::shared_ptr<const StateGraphDefinition> SkeletonStateGraphLoader::CompileDefinition(Json& document) {
	for (const auto* name : { "Parameters", "Clips", "States" }) {
		require(document.at(name).is_object(), std::string(name) + " must be an object");
	}
	for (const auto& [name, state] : document.at("States").items()) {
		require(!state.contains("Action"), "state Action metadata is unsupported; configure traversal actions separately");
	}
	const auto descriptor = serde::deserialize<StateGraphDescriptor>(document);
	const auto skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(
		descriptor.SkeletonPath, descriptor.SkeletonPath.ends_with(".skeleton") ? ELoadingType::RESOURCE : ELoadingType::FILE);
	require(bool(skeleton), "missing skeleton");
	require(skeleton->getNumJolts() <= MAXBONES, "skeleton exceeds pose capacity");
	require(skeleton->getNumJolts() == skeleton->joints().size(), "inconsistent skeleton joint count");
	for (size_t i = 0; i < skeleton->joints().size(); ++i) {
		require(skeleton->joints()[i].parentIndex < int(i), "skeleton must be ordered by parent");
	}
	auto result = std::make_shared<StateGraphDefinition>();
	result->skeleton = std::make_shared<Skeleton>(*skeleton);
	result->dependencies = descriptor.Dependencies;
	result->dependencies.push_back(descriptor.SkeletonPath);
	const auto& bindings = descriptor.Bindings;
	for (const auto& [name, parameter] : descriptor.Parameters) {
		const auto value = parameterValue(parameter);
		const bool trigger = parameter.Type == "trigger";
		result->parameters.push_back({ name, value, trigger });
	}
	for (const auto* name : { "StateTime", "NormalizedTime", "Duration", "Finished" }) {
		require(!descriptor.Parameters.contains(name) && !bindings.contains(name), "reserved parameter name " + std::string(name));
	}
	std::set<std::string> visiting;
	std::function<void(const Json&, int)> expression;
	expression = [&](const Json& e, int depth) {
		require(depth < 64, "expression cycle or nesting exceeds 64");
		if (e.is_boolean() || e.is_number()) {
			require(!e.is_number() || std::isfinite(e.get<double>()), "non-finite constant");
			return;
		}
		if (e.is_string()) {
			const auto name = e.get<std::string>();
			if (name == "StateTime" || name == "NormalizedTime" || name == "Finished" || name == "Duration") {
				return;
			}
			if (bindings.contains(name)) {
				require(visiting.insert(name).second, "binding cycle at " + name);
				const auto& binding = bindings.at(name);
				expression(binding.Expression, depth + 1);
				visiting.erase(name);
			}
			else {
				require(descriptor.Parameters.contains(name), "undeclared parameter " + name);
			}
			return;
		}
		require(e.is_array() && !e.empty() && e[0].is_string(), "invalid expression " + e.dump());
		const auto op = e[0].get<std::string>();
		const auto it = operators.find(op);
		require(it != operators.end(), "unknown operator " + op);
		require(e.size() >= size_t(it->second.first + 1) && e.size() <= size_t(it->second.second + 1), "wrong arity for " + op);
		for (size_t i = 1; i < e.size(); ++i) {
			expression(e[i], depth + 1);
		}
	};
	for (const auto& [name, binding] : bindings) {
		require(!descriptor.Parameters.contains(name), "binding shadows parameter " + name);
		require(std::isfinite(binding.Smoothing) && binding.Smoothing >= 0, "invalid binding Smoothing");
		expression(binding.Expression, 0);
	}
	const auto& clips = descriptor.Clips;
	require(!clips.empty(), "Clips must be a nonempty object");
	const auto rig = descriptor.Rig.value_or(StateGraphRigDescriptor {});
	const int root = skeleton->findJointIndex(rig.Root);
	const float scale = rig.UnitsToMeters;
	require(scale > 0 && std::isfinite(scale), "invalid rig scale");
	result->ir.rig.rootJoint = root;
	result->ir.rig.unitsToMeters = scale;
	result->ir.rig.rootHeight = rig.RootHeight;
	if (descriptor.Rig) {
		require(root >= 0, "missing rig root joint");
		for (const auto* joints : { &rig.Feet, &rig.Hands }) {
			for (const auto& joint : *joints) {
				require(skeleton->findJointIndex(joint) >= 0, "missing rig joint " + joint);
			}
		}
	}
	std::map<std::string, uint16_t> clipIndex;
	for (const auto& [name, c] : clips) {
		auto source = c.Path.ends_with(".animation") ? SkeletonAnimationLoader::CreateFromResource(c.Path, skeleton)
													 : SkeletonAnimationLoader::CreateFromFile(c.Path, skeleton);
		require(bool(source), "cannot load clip " + name);
		require(source->ticksPerSecond > 0 && source->durationInTicks > 0, "invalid clip duration: " + name);
		auto prepared = std::make_shared<Animation>(*source);
		require(prepared->channels.size() <= skeleton->getNumJolts(), "clip has incompatible joint count: " + name);
		require(std::any_of(prepared->channels.begin(), prepared->channels.end(),
					[](const auto& ch) { return !ch.translationKeyframes.empty() || !ch.rotationKeyframes.empty() || !ch.scaleKeyframes.empty(); }),
			"clip contains no matching animated joints: " + name);
		for (size_t i = 0; i < prepared->channels.size(); ++i) {
			if (!prepared->channels[i].jointName.empty()) {
				require(trimmedName(prepared->channels[i].jointName) == trimmedName(skeleton->joints()[i].name), "clip joint mismatch: " + name);
			}
		}
		auto meta = readClipMeta(name, c);
		const auto mode = meta.rootMode;
		if (root >= 0 && root < int(prepared->channels.size())) {
			auto& channel = prepared->channels[root];
			auto& keys = channel.translationKeyframes;
			if (!keys.empty() && mode != ClipRootMode::Traversal && mode != ClipRootMode::Authored) {
				const auto first = keys.front(), last = keys.back();
				for (auto& k : keys) {
					k.translation.x = 0;
					k.translation.z = 0;
					if (mode == ClipRootMode::Stairs) {
						k.translation.y
							-= (last.translation.y - first.translation.y) * float((k.time - first.time) / std::max(.0001, last.time - first.time));
					}
					if (mode == ClipRootMode::Flat) {
						k.translation.y = rig.RootHeight;
					}
				}
			}
			if (c.RemoveYaw) {
				for (auto& key : channel.rotationKeyframes) {
					const auto forward = key.rotation * MATH::Vector3f { 0, 0, 1 };
					key.rotation = MATH::QuaternionF(MATH::Vector3f { 0, -std::atan2(forward.x, forward.z) * 57.2957795f, 0 }) * key.rotation;
				}
			}
		}
		clipIndex[name] = uint16_t(result->ir.clips.size());
		result->ir.clips.push_back(meta);
		result->clips.emplace(name, std::move(prepared));
		result->dependencies.push_back(c.Path);
		require(std::isfinite(meta.phaseOffset), "invalid PhaseOffset");
	}
	const auto& states = descriptor.States;
	require(!states.empty(), "empty States");
	require(states.contains(descriptor.InitialState), "missing InitialState");
	for (const auto& [name, state] : states) {
		require(clipIndex.contains(state.Clock), "missing clock clip in " + name);
		if (state.Rate) {
			expression(*state.Rate, 0);
		}
		if (state.EntryTime) {
			expression(*state.EntryTime, 0);
		}
		if (state.ExternalTime) {
			expression(*state.ExternalTime, 0);
		}
	}
	std::set<std::string> phased, phaseStack;
	std::function<void(const std::string&)> matchPhase;
	matchPhase = [&](const std::string& name) {
		if (phased.contains(name)) {
			return;
		}
		require(phaseStack.insert(name).second, "phase-reference cycle at " + name);
		const auto& clip = clips.at(name);
		if (clip.PhaseReference) {
			const auto& reference = *clip.PhaseReference;
			require(clips.contains(reference), "unknown phase reference " + reference);
			matchPhase(reference);
			const auto& joints = rig.PhaseJoints;
			require(joints.size() == 2, "PhaseJoints needs two support joints");
			const int left = skeleton->findJointIndex(joints[0]), right = skeleton->findJointIndex(joints[1]);
			require(left >= 0 && right >= 0, "missing phase support joints");
			auto stride = [&](const std::string& id) {
				AnimSample sample(result->skeleton, result->clips.at(id));
				std::array<float, 32> values {};
				for (int i = 0; i < 32; ++i) {
					sample.seek(sample.duration() * float(i) / 32);
					const auto p = CHARACTER::positions(*result->skeleton, *sample.getPose());
					values[i] = p[left].y - p[right].y;
				}
				return values;
			};
			result->ir.clips[clipIndex.at(name)].phaseOffset
				= CHARACTER::strideOffset(stride(reference), stride(name)) + result->ir.clips[clipIndex.at(reference)].phaseOffset + clip.PhaseOffset;
		}
		phaseStack.erase(name);
		phased.insert(name);
	};
	for (const auto& [name, c] : clips) {
		matchPhase(name);
	}
	for (const auto& parameter : result->parameters) {
		const auto type = std::visit([](auto value) { return DATA::typeOf(DATA::Value(value)); }, parameter.defaultValue);
		result->symbols.emplace(parameter.name, DATA::Symbol { result->symbols.size(), type });
	}
	result->parameterCount = result->symbols.size();
	result->builtinStart = result->symbols.size();
	for (const auto* name : { "StateTime", "NormalizedTime", "Duration", "Finished" }) {
		result->symbols.emplace(
			name, DATA::Symbol { result->symbols.size(), std::string_view(name) == "Finished" ? DATA::ValueType::Bool : DATA::ValueType::Float });
	}
	std::set<std::string> building;
	std::function<void(const std::string&)> compileBinding;
	compileBinding = [&](const std::string& name) {
		if (result->symbols.contains(name)) {
			return;
		}
		require(building.insert(name).second, "binding cycle: " + name);
		const auto& b = bindings.at(name);
		const auto& e = b.Expression;
		std::function<void(const Json&)> deps = [&](const Json& x) {
			if (x.is_string() && bindings.contains(x.get<std::string>())) {
				compileBinding(x.get<std::string>());
			}
			else if (x.is_array()) {
				for (size_t i = 1; i < x.size(); ++i) {
					deps(x[i]);
				}
			}
		};
		deps(e);
		auto program = DATA::Expression::compile(e, result->symbols);
		const auto slot = result->symbols.size();
		const float response = b.Smoothing;
		require(response == 0 || program.type() == DATA::ValueType::Float, "Smoothing needs Float");
		result->symbols.emplace(name, DATA::Symbol { slot, program.type() });
		result->bindings.push_back({ slot, std::move(program), response });
		building.erase(name);
	};
	for (const auto& [name, b] : bindings) {
		compileBinding(name);
	}
	auto compileProgram = [&](const Json& e, bool expectBool) -> uint16_t {
		auto program = DATA::Expression::compile(e, result->symbols);
		require(
			expectBool ? program.type() == DATA::ValueType::Bool : program.type() == DATA::ValueType::Float || program.type() == DATA::ValueType::Int,
			"wrong expression result type");
		const auto id = uint16_t(result->expressions.size());
		result->expressions.push_back(std::move(program));
		return id;
	};
	std::function<uint16_t(const Json&, int)> compileNode;
	compileNode = [&](const Json& n, int depth) -> uint16_t {
		require(depth < 32, "node nesting exceeds 32");
		if (n.is_string()) {
			const auto name = n.get<std::string>();
			require(clipIndex.contains(name), "unknown clip " + name);
			ClipNode node { clipIndex.at(name) };
			result->ir.nodes.push_back(node);
			return uint16_t(result->ir.nodes.size() - 1);
		}
		if (n.contains("Blend")) {
			require(n.at("Blend").is_array() && n.at("Blend").size() == 2, "Blend needs two nodes");
			BlendNode node { compileNode(n["Blend"][0], depth + 1), compileNode(n["Blend"][1], depth + 1), compileProgram(n.at("Weight"), false) };
			result->ir.nodes.push_back(node);
			return uint16_t(result->ir.nodes.size() - 1);
		}
		if (n.contains("Select")) {
			SelectNode node { compileProgram(n.at("Select"), true), compileNode(n.at("True"), depth + 1), compileNode(n.at("False"), depth + 1) };
			result->ir.nodes.push_back(node);
			return uint16_t(result->ir.nodes.size() - 1);
		}
		if (n.contains("Blendspace1D")) {
			const auto& points = n.at("Blendspace1D");
			require(points.is_array() && !points.empty(), "empty blendspace");
			float last = -INFINITY;
			Blendspace1DNode node;
			node.parameterProgram = compileProgram(n.at("Parameter"), false);
			for (const auto& p : points) {
				const float v = p.at("Value");
				require(std::isfinite(v) && v > last, "blendspace points must increase strictly");
				last = v;
				node.points.push_back({ v, compileNode(p.at("Node"), depth + 1) });
			}
			result->ir.nodes.push_back(std::move(node));
			return uint16_t(result->ir.nodes.size() - 1);
		}
		require(false, "unknown blend node " + n.dump());
		return 0;
	};
	for (const auto& [name, s] : states) {
		CompiledState state;
		state.name = name;
		state.nodeIndex = compileNode(s.Node, 0);
		state.clockClipIndex = clipIndex.at(s.Clock);
		if (s.Rate) {
			state.rateProgram = compileProgram(*s.Rate, false);
		}
		if (s.EntryTime) {
			state.entryTimeProgram = compileProgram(*s.EntryTime, false);
		}
		if (s.ExternalTime) {
			state.externalTimeProgram = compileProgram(*s.ExternalTime, false);
		}
		state.applyPoseOffset = s.ApplyPoseOffset;
		state.preserveTime = s.PreserveTime;
		state.lockTransitions = s.LockTransitions;
		state.syncGroup = s.SyncGroup;
		result->ir.stateByName[name] = result->ir.states.size();
		result->ir.states.push_back(std::move(state));
	}
	result->ir.initialStateIndex = result->ir.stateByName.at(descriptor.InitialState);
	for (const auto& layerDesc : descriptor.Layers) {
		CompiledLayer layer;
		layer.mode = parseLayerMode(layerDesc.Mode);
		layer.defaultWeight = layerDesc.Weight;
		layer.boneMask = compileBoneMask(*skeleton, layerDesc.Mask, layerDesc.MaskJoints);
		layer.nodeIndex = compileNode(layerDesc.Node, 0);
		if (layerDesc.WeightExpression) {
			layer.weightProgram = compileProgram(*layerDesc.WeightExpression, false);
		}
		result->ir.layers.push_back(layer);
	}
	auto edge = [&](const StateGraphTransitionDescriptor& transition) {
		require(states.contains(transition.Target), "unknown transition target");
		expression(transition.When, 0);
		require(std::isfinite(transition.BlendTime) && transition.BlendTime >= 0, "invalid BlendTime");
		std::vector<size_t> consumeSlots;
		for (const auto& name : transition.Consume) {
			const auto parameter = descriptor.Parameters.find(name);
			require(parameter != descriptor.Parameters.end() && parameter->second.Type == "trigger", "Consume requires declared triggers");
			consumeSlots.push_back(result->symbols.at(name).slot);
		}
		return DATA::StateTransition { transition.Target, DATA::Expression::compile(transition.When, result->symbols), transition.Priority,
			transition.Interrupt, transition.Force, transition.BlendTime, std::move(consumeSlots), transition.displayText };
	};
	for (const auto& [name, state] : states) {
		for (const auto& transition : state.Transitions) {
			result->ir.machine.transitions[name].push_back(edge(transition));
		}
	}
	for (const auto& transition : descriptor.AnyState) {
		result->ir.machine.anyState.push_back(edge(transition));
	}
	return result;
}
}
