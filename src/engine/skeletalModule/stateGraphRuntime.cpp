#include "stateGraphRuntime.h"
#include "characterAnimation.h"
#include "stateGraphDescriptor.h"
#include <algorithm>
#include <cmath>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace IKIGAI::SKELETON {
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
	UTILS::VarValue parameterValue(const Json& p) {
		const auto type = p.at("Type").get<std::string>();
		if (p.contains("Default")) {
			const auto& d = p.at("Default");
			require(type == "float" ? d.is_number() : type == "int" ? d.is_number_integer() : d.is_boolean(), "parameter default has wrong type");
		}
		if (type == "float") {
			return p.value("Default", 0.f);
		}
		if (type == "int") {
			return p.value("Default", 0);
		}
		if (type == "bool" || type == "trigger") {
			return p.value("Default", false);
		}
		throw std::runtime_error("State graph: unknown parameter type " + type);
	}
	void declareParameters(const Json& doc, UTILS::Environment& env) {
		for (const auto& [name, p] : doc.at("Parameters").items()) {
			env.declare(name, parameterValue(p), p.at("Type") == "trigger");
		}
	}
	CompiledClipMeta readClipMeta(const std::string& name, const Json& c) {
		CompiledClipMeta meta;
		meta.name = name;
		try {
			ClipDescriptor desc;
			serde::from_json(c, desc);
			meta.loop = desc.Loop;
			meta.phaseOffset = desc.PhaseOffset;
			meta.rootMode = parseClipRootMode(desc.RootMode);
			meta.rootMotion = parseRootMotionPolicy(desc.RootMotion);
			meta.removeYaw = desc.RemoveYaw;
			for (const auto& event : desc.Events) {
				require(std::isfinite(event.Time), "invalid clip event time in " + name);
				meta.events.push_back({ event.Time, event.Name });
			}
		} catch (const std::exception&) {
			meta.loop = c.value("Loop", true);
			meta.phaseOffset = c.value("PhaseOffset", 0.f);
			meta.rootMode = parseClipRootMode(c.value("RootMode", std::string("in-place")));
			meta.rootMotion = parseRootMotionPolicy(c.value("RootMotion", std::string("in-pose")));
			meta.removeYaw = c.value("RemoveYaw", false);
		}
		return meta;
	}
}

std::shared_ptr<const StateGraphDefinition> StateGraphDefinition::compile(
	const Json& doc, std::shared_ptr<Skeleton> skeleton, const std::function<std::shared_ptr<Animation>(const std::string&)>& load) {
	require(doc.value("Version", 0) == 2, "unsupported Version (expected 2)");
	require(bool(skeleton), "missing skeleton");
	require(skeleton->getNumJolts() <= MAXBONES, "skeleton exceeds pose capacity");
	require(skeleton->getNumJolts() == skeleton->joints().size(), "inconsistent skeleton joint count");
	for (size_t i = 0; i < skeleton->joints().size(); ++i) {
		require(skeleton->joints()[i].parentIndex < int(i), "skeleton must be ordered by parent");
	}
	auto result = std::make_shared<StateGraphDefinition>();
	result->document = doc;
	result->skeleton = std::make_shared<Skeleton>(*skeleton);
	result->dependencies = doc.value("Dependencies", std::vector<std::string> {});
	UTILS::Environment env;
	declareParameters(doc, env);
	for (const auto* name : { "StateTime", "NormalizedTime", "Duration", "Finished" }) {
		require(!doc.at("Parameters").contains(name) && (!doc.contains("Bindings") || !doc.at("Bindings").contains(name)),
			"reserved parameter name " + std::string(name));
	}
	const auto bindings = doc.value("Bindings", Json::object());
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
				expression(binding.is_object() ? binding.at("Expression") : binding, depth + 1);
				visiting.erase(name);
			}
			else {
				require(env.hasVariable(name), "undeclared parameter " + name);
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
	for (const auto& [name, e] : bindings.items()) {
		require(!env.hasVariable(name), "binding shadows parameter " + name);
		if (e.is_object()) {
			const float response = e.value("Smoothing", 0.f);
			require(std::isfinite(response) && response >= 0, "invalid binding Smoothing");
			expression(e.at("Expression"), 0);
		}
		else {
			expression(e, 0);
		}
	}
	const auto& clips = doc.at("Clips");
	require(clips.is_object() && !clips.empty(), "Clips must be a nonempty object");
	const auto rig = doc.value("Rig", Json::object());
	const int root = skeleton->findJointIndex(rig.value("Root", std::string("Hips")));
	const float scale = rig.value("UnitsToMeters", .01f);
	require(scale > 0 && std::isfinite(scale), "invalid rig scale");
	result->ir.rig.rootJoint = root;
	result->ir.rig.unitsToMeters = scale;
	result->ir.rig.rootHeight = rig.value("RootHeight", 90.f);
	if (doc.contains("Rig")) {
		require(root >= 0, "missing rig root joint");
		for (const auto key : { "Feet", "Hands" }) {
			if (rig.contains(key)) {
				for (const auto& joint : rig.at(key)) {
					require(skeleton->findJointIndex(joint.get<std::string>()) >= 0, "missing rig joint " + joint.get<std::string>());
				}
			}
		}
	}
	std::map<std::string, uint16_t> clipIndex;
	for (const auto& [name, c] : clips.items()) {
		auto source = load(c.at("Path").get<std::string>());
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
		const auto mode = c.value("RootMode", std::string("in-place"));
		require(mode == "in-place" || mode == "stairs" || mode == "flat" || mode == "traversal" || mode == "authored", "unknown RootMode: " + mode);
		if (root >= 0 && root < int(prepared->channels.size())) {
			auto& channel = prepared->channels[root];
			auto& keys = channel.translationKeyframes;
			if (!keys.empty() && mode != "traversal" && mode != "authored") {
				const auto first = keys.front(), last = keys.back();
				for (auto& k : keys) {
					k.translation.x = 0;
					k.translation.z = 0;
					if (mode == "stairs") {
						k.translation.y
							-= (last.translation.y - first.translation.y) * float((k.time - first.time) / std::max(.0001, last.time - first.time));
					}
					if (mode == "flat") {
						k.translation.y = rig.value("RootHeight", 90.f);
					}
				}
			}
			if (c.value("RemoveYaw", false)) {
				for (auto& key : channel.rotationKeyframes) {
					const auto forward = key.rotation * MATH::Vector3f { 0, 0, 1 };
					key.rotation = MATH::QuaternionF(MATH::Vector3f { 0, -std::atan2(forward.x, forward.z) * 57.2957795f, 0 }) * key.rotation;
				}
			}
		}
		clipIndex[name] = uint16_t(result->ir.clips.size());
		result->ir.clips.push_back(meta);
		result->clips.emplace(name, std::move(prepared));
		result->dependencies.push_back(c.at("Path"));
		require(std::isfinite(meta.phaseOffset), "invalid PhaseOffset");
	}
	const auto& states = doc.at("States");
	require(states.is_object() && !states.empty(), "empty States");
	require(states.contains(doc.at("InitialState").get<std::string>()), "missing InitialState");
	auto transition = [&](const Json& t) {
		require(states.contains(t.at("Target").get<std::string>()), "unknown transition target");
		expression(t.value("When", Json(true)), 0);
		const float fade = t.value("BlendTime", .15f);
		require(std::isfinite(fade) && fade >= 0, "invalid BlendTime");
		if (t.contains("Consume")) {
			for (const auto& name : t.at("Consume")) {
				require(doc.at("Parameters").contains(name.get<std::string>()) && doc["Parameters"][name.get<std::string>()]["Type"] == "trigger",
					"Consume requires declared triggers");
			}
		}
	};
	for (const auto& t : doc.value("AnyState", Json::array())) {
		transition(t);
	}
	for (const auto& [name, s] : states.items()) {
		require(clipIndex.contains(s.at("Clock").get<std::string>()), "missing clock clip in " + name);
		expression(s.value("Rate", Json(1)), 0);
		expression(s.value("EntryTime", Json(0)), 0);
		if (s.contains("ExternalTime")) {
			expression(s.at("ExternalTime"), 0);
		}
		for (const auto& t : s.value("Transitions", Json::array())) {
			transition(t);
		}
		require(!s.contains("Action"), "state Action metadata is unsupported; configure traversal actions separately");
	}
	std::set<std::string> phased, phaseStack;
	std::function<void(const std::string&)> matchPhase;
	matchPhase = [&](const std::string& name) {
		if (phased.contains(name)) {
			return;
		}
		require(phaseStack.insert(name).second, "phase-reference cycle at " + name);
		const auto& clip = clips.at(name);
		if (clip.contains("PhaseReference")) {
			const auto reference = clip.at("PhaseReference").get<std::string>();
			require(clips.contains(reference), "unknown phase reference " + reference);
			matchPhase(reference);
			const auto joints = rig.value("PhaseJoints", std::vector<std::string> { "LeftFoot", "RightFoot" });
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
			result->document["Clips"][name]["PhaseOffset"] = CHARACTER::strideOffset(stride(reference), stride(name))
				+ result->document["Clips"][reference].value("PhaseOffset", 0.f) + clip.value("PhaseOffset", 0.f);
			result->ir.clips[clipIndex.at(name)].phaseOffset = result->document["Clips"][name]["PhaseOffset"];
		}
		phaseStack.erase(name);
		phased.insert(name);
	};
	for (const auto& [name, c] : clips.items()) {
		matchPhase(name);
	}
	for (const auto& [name, p] : doc.at("Parameters").items()) {
		auto value = parameterValue(p);
		const auto type = std::visit([](auto v) { return DATA::typeOf(DATA::Value(v)); }, value);
		result->symbols.emplace(name, DATA::Symbol { result->symbols.size(), type });
		result->parameters.push_back(name);
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
		const auto& e = b.is_object() ? b.at("Expression") : b;
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
		const float response = b.is_object() ? b.value("Smoothing", 0.f) : 0.f;
		require(response == 0 || program.type() == DATA::ValueType::Float, "Smoothing needs Float");
		result->symbols.emplace(name, DATA::Symbol { slot, program.type() });
		result->bindings.push_back({ slot, std::move(program), response });
		building.erase(name);
	};
	for (const auto& [name, b] : bindings.items()) {
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
	for (const auto& [name, s] : states.items()) {
		CompiledState state;
		state.name = name;
		state.nodeIndex = compileNode(s.at("Node"), 0);
		state.clockClipIndex = clipIndex.at(s.at("Clock").get<std::string>());
		if (s.contains("Rate")) {
			state.rateProgram = compileProgram(s.at("Rate"), false);
		}
		if (s.contains("EntryTime")) {
			state.entryTimeProgram = compileProgram(s.value("EntryTime", Json(0)), false);
		}
		if (s.contains("ExternalTime")) {
			state.externalTimeProgram = compileProgram(s.at("ExternalTime"), false);
		}
		state.applyPoseOffset = s.value("ApplyPoseOffset", true);
		state.preserveTime = s.value("PreserveTime", false);
		state.lockTransitions = s.value("LockTransitions", false);
		state.syncGroup = s.value("SyncGroup", std::string {});
		result->ir.stateByName[name] = result->ir.states.size();
		result->ir.states.push_back(std::move(state));
	}
	result->ir.initialStateIndex = result->ir.stateByName.at(doc.at("InitialState").get<std::string>());
	if (doc.contains("Layers")) {
		for (const auto& layerJson : doc.at("Layers")) {
			LayerDescriptor layerDesc;
			serde::from_json(layerJson, layerDesc);
			CompiledLayer layer;
			layer.mode = parseLayerMode(layerDesc.Mode);
			layer.defaultWeight = layerDesc.Weight;
			layer.boneMask = compileBoneMask(*skeleton, layerDesc.Mask, layerDesc.MaskJoints);
			layer.nodeIndex = compileNode(layerDesc.Node, 0);
			if (layerJson.contains("WeightExpression")) {
				layer.weightProgram = compileProgram(layerJson.at("WeightExpression"), false);
			}
			result->ir.layers.push_back(layer);
		}
	}
	auto edge = [&](const Json& t) {
		return DATA::StateTransition { t.at("Target").get<std::string>(), DATA::Expression::compile(t.value("When", Json(true)), result->symbols),
			t.value("Priority", 0), t.value("Interrupt", false), t.value("Force", false), t.value("BlendTime", .15f), t };
	};
	for (const auto& [name, state] : doc.at("States").items()) {
		for (const auto& t : state.value("Transitions", Json::array())) {
			result->ir.machine.transitions[name].push_back(edge(t));
		}
	}
	for (const auto& t : doc.value("AnyState", Json::array())) {
		result->ir.machine.anyState.push_back(edge(t));
	}
	return result;
}

StateGraphRuntime::StateGraphRuntime(std::shared_ptr<const StateGraphDefinition> def, std::shared_ptr<UTILS::Environment> env)
	: mDefinition(std::move(def))
	, mEnvironment(std::move(env))
	, mBlend(mDefinition->skeleton) {
	declareParameters(definition(), *mEnvironment);
	for (const auto& name : mDefinition->parameters) {
		mParameterSlots.push_back(mEnvironment->slot(name));
	}
	mExpressionValues.resize(mDefinition->symbols.size(), 0.f);
	for (const auto& clip : mDefinition->ir.clips) {
		auto sample = std::make_shared<AnimSample>(mDefinition->skeleton, mDefinition->clips.at(clip.name));
		sample->setLooping(clip.loop);
		sample->seek(0);
		mSamples[clip.name] = sample;
	}
	enter(mDefinition->ir.initialStateIndex, 0);
	updateBindings(0);
	mOutput = sampleNode(activeState().nodeIndex, mPlaybackTime, 0);
}
StateGraphRuntime::StateGraphRuntime(const StateGraphRuntime& other)
	: DATA::StateMachineInstance(other)
	, mDefinition(other.mDefinition)
	, mEnvironment(std::make_shared<UTILS::Environment>(other.mEnvironment->snapshot()))
	, mPhases(other.mPhases)
	, mSmoothedBindings(other.mSmoothedBindings)
	, mExpressionValues(other.mExpressionValues)
	, mParameterSlots(other.mParameterSlots)
	, mSavedTimes(other.mSavedTimes)
	, mStateIndex(other.mStateIndex)
	, mFiredEvents(other.mFiredEvents)
	, mBindRootPose(other.mBindRootPose)
	, mLastRootPose(other.mLastRootPose)
	, mHasBindRoot(other.mHasBindRoot)
	, mRootMotion(other.mRootMotion)
	, mPendingEvents(other.mPendingEvents)
	, mSnapshotBlend(other.mSnapshotBlend)
	, mPlaybackTime(other.mPlaybackTime)
	, mPreviousPlaybackTime(other.mPreviousPlaybackTime)
	, mOutput(other.mOutput)
	, mPreviousPose(other.mPreviousPose)
	, mLastTransition(other.mLastTransition)
	, mPoseOffset(other.mPoseOffset)
	, mBlend(other.mDefinition->skeleton) {
	for (const auto& [name, s] : other.mSamples) {
		mSamples[name] = std::shared_ptr<AnimSample>(static_cast<AnimSample*>(s->clone().release()));
	}
}
const CompiledState& StateGraphRuntime::stateResource() const {
	return mDefinition->ir.states.at(mStateIndex);
}
const StateGraphRuntime::Json& StateGraphRuntime::metadata() const {
	return definition().at("States").at(mState);
}
void StateGraphRuntime::reset() {
	for (const auto& [name, p] : definition().at("Parameters").items()) {
		mEnvironment->setValue(name, parameterValue(p));
	}
	mPhases.clear();
	mSmoothedBindings.clear();
	mSavedTimes.clear();
	mState.clear();
	mPreviousState.clear();
	mOutput = {};
	mPoseOffset = {};
	mLastTransition = {};
	mHasBindRoot = false;
	mRootMotion = {};
	mPendingEvents.clear();
	enter(mDefinition->ir.initialStateIndex, 0);
	update(0);
}
DATA::Value StateGraphRuntime::readExpressionSlot(size_t slot) const {
	if (slot < mDefinition->parameterCount) {
		return std::visit([](auto v) -> DATA::Value { return v; }, mEnvironment->get(mParameterSlots.at(slot)));
	}
	const auto built = slot - mDefinition->builtinStart;
	if (built < 4) {
		const auto& s = activeState();
		const auto& clock = mDefinition->ir.clips[s.clockClipIndex];
		const float duration = mSamples.at(clock.name)->duration();
		switch (built) {
		case 0:
			return mTime;
		case 1:
			return mPlaybackTime / duration;
		case 2:
			return duration;
		case 3:
			return !clock.loop && mPlaybackTime >= duration;
		}
	}
	return mExpressionValues.at(slot);
}
float StateGraphRuntime::evaluateProgram(uint16_t program) const {
	const auto result = mDefinition->expressions.at(program).evaluate([&](size_t slot) { return readExpressionSlot(slot); });
	return std::holds_alternative<bool>(result) ? (std::get<bool>(result) ? 1.f : 0.f) : DATA::number(result);
}
void StateGraphRuntime::enter(size_t stateIndex, float blend) {
	const auto& state = mDefinition->ir.states.at(stateIndex);
	if (stateIndex == mStateIndex && !mState.empty()) {
		return;
	}
	if (!mState.empty()) {
		mSavedTimes[mState] = { mTime, mPlaybackTime };
	}
	mSnapshotBlend = blendWeight() < 1 || mPoseOffset.x != 0 || mPoseOffset.y != 0 || mPoseOffset.z != 0;
	mPreviousPose = mOutput;
	mPreviousState = mState;
	mPreviousTime = mTime;
	mPreviousPlaybackTime = mPlaybackTime;
	mStateIndex = stateIndex;
	mState = state.name;
	mTime = 0;
	mPlaybackTime = 0;
	mBlendTime = 0;
	mBlendDuration = mPreviousState.empty() ? 0 : blend;
	mHasBindRoot = false;
	mFiredEvents.assign(mDefinition->ir.clips[state.clockClipIndex].events.size(), 0);
	if (state.preserveTime && mSavedTimes.contains(mState)) {
		mTime = mSavedTimes[mState].first;
		mPlaybackTime = mSavedTimes[mState].second;
	}
	else if (state.entryTimeProgram) {
		mPlaybackTime = std::max(0.f, evaluateProgram(*state.entryTimeProgram));
	}
}
float StateGraphRuntime::sampleTime(uint16_t clipIndex, float time) const {
	const auto& s = activeState();
	const auto& clipMeta = mDefinition->ir.clips[clipIndex];
	const float duration = mSamples.at(clipMeta.name)->duration();
	if (s.externalTimeProgram && clipIndex == s.clockClipIndex) {
		return evaluateProgram(*s.externalTimeProgram);
	}
	if (clipMeta.loop && !s.syncGroup.empty()) {
		const float p = (mPhases.contains(s.syncGroup) ? mPhases.at(s.syncGroup) : 0) + clipMeta.phaseOffset;
		return (p - std::floor(p)) * duration;
	}
	return time;
}
Pose StateGraphRuntime::sampleNode(uint16_t nodeIndex, float time, float dt) {
	const auto& node = mDefinition->ir.nodes.at(nodeIndex);
	if (const auto* clip = std::get_if<ClipNode>(&node)) {
		const auto& meta = mDefinition->ir.clips[clip->clipIndex];
		auto& sample = mSamples.at(meta.name);
		sample->seek(sampleTime(clip->clipIndex, time));
		return *sample->getPose();
	}
	if (const auto* select = std::get_if<SelectNode>(&node)) {
		const bool pickTrue
			= DATA::boolean(mDefinition->expressions.at(select->conditionProgram).evaluate([&](size_t slot) { return readExpressionSlot(slot); }));
		return sampleNode(pickTrue ? select->whenTrue : select->whenFalse, time, dt);
	}
	if (const auto* blend = std::get_if<BlendNode>(&node)) {
		auto low = sampleNode(blend->childA, time, dt), high = sampleNode(blend->childB, time, dt);
		return *mBlend.blend(&low, &high, std::clamp(evaluateProgram(blend->weightProgram), 0.f, 1.f));
	}
	const auto& space = *std::get_if<Blendspace1DNode>(&node);
	const float v = evaluateProgram(space.parameterProgram);
	if (v <= space.points.front().value) {
		return sampleNode(space.points.front().nodeIndex, time, dt);
	}
	for (size_t i = 1; i < space.points.size(); ++i) {
		if (v < space.points[i].value) {
			auto low = sampleNode(space.points[i - 1].nodeIndex, time, dt), high = sampleNode(space.points[i].nodeIndex, time, dt);
			const float x = space.points[i - 1].value, y = space.points[i].value;
			return *mBlend.blend(&low, &high, (v - x) / (y - x));
		}
	}
	return sampleNode(space.points.back().nodeIndex, time, dt);
}
void StateGraphRuntime::dispatchClockEvents(float previousTime, float currentTime, float rate) {
	if (rate <= 0) {
		return;
	}
	const auto& clock = mDefinition->ir.clips[activeState().clockClipIndex];
	if (clock.events.empty()) {
		return;
	}
	const float duration = mSamples.at(clock.name)->duration();
	auto fireRange = [&](float from, float to) {
		for (size_t i = 0; i < clock.events.size(); ++i) {
			if (mFiredEvents[i]) {
				continue;
			}
			const float at = clock.events[i].timeSeconds;
			if (at > from && at <= to) {
				mFiredEvents[i] = 1;
				mPendingEvents.push_back({ clock.events[i].name, clock.name, at });
			}
		}
	};
	if (clock.loop && currentTime < previousTime) {
		fireRange(previousTime, duration);
		fireRange(0.f, currentTime);
	}
	else {
		fireRange(previousTime, currentTime);
	}
}
void StateGraphRuntime::applyRootMotionExtraction(Pose& pose) {
	const auto& clock = mDefinition->ir.clips[activeState().clockClipIndex];
	if (clock.rootMotion != RootMotionPolicy::Extract) {
		mRootMotion.valid = false;
		return;
	}
	const int root = mDefinition->ir.rig.rootJoint;
	if (root < 0 || root >= int(pose.numKeyframes)) {
		mRootMotion.valid = false;
		return;
	}
	auto& joint = pose.keyframes[root];
	if (!mHasBindRoot) {
		mBindRootPose = joint;
		mLastRootPose = joint;
		mHasBindRoot = true;
		mRootMotion.valid = false;
		return;
	}
	const float scale = mDefinition->ir.rig.unitsToMeters;
	mRootMotion.translationDeltaMeters = (joint.translation - mLastRootPose.translation) * scale;
	mRootMotion.rotationDelta = rotationDelta(mLastRootPose.rotation, joint.rotation);
	mRootMotion.valid = true;
	mLastRootPose = joint;
	joint.translation = mBindRootPose.translation;
	joint.rotation = mBindRootPose.rotation;
}
void StateGraphRuntime::applyLayerStack(Pose& pose, float time, float dt) {
	(void)dt;
	for (const auto& layer : mDefinition->ir.layers) {
		mLayerScratch = sampleNode(layer.nodeIndex, time, 0);
		const float weight = layer.weightProgram ? evaluateProgram(*layer.weightProgram) : layer.defaultWeight;
		const float t = std::clamp(weight, 0.f, 1.f);
		if (t <= 0) {
			continue;
		}
		if (layer.mode == AnimationLayerMode::Override) {
			pose = *mBlend.blendMasked(&pose, &mLayerScratch, t, layer.boneMask);
		}
		else {
			pose = *mBlend.blendMaskedAdditive(&pose, &mLayerScratch, t, layer.boneMask);
		}
	}
}
void StateGraphRuntime::update(float dt) {
	require(std::isfinite(dt) && dt >= 0, "invalid delta time");
	updateBindings(dt);
	const auto& active = activeState();
	const auto* selected = select(mDefinition->ir.machine, active.lockTransitions, [&](size_t slot) { return readExpressionSlot(slot); });
	if (selected) {
		mLastTransition = selected->metadata;
		const auto it = mDefinition->ir.stateByName.find(selected->target);
		require(it != mDefinition->ir.stateByName.end(), "transition target missing from IR");
		enter(it->second, selected->blend);
		for (const auto& name : selected->metadata.value("Consume", Json::array())) {
			mEnvironment->setValue(name.get<std::string>(), false);
		}
	}
	mPreviousPlaybackTime = mPlaybackTime;
	mTime += dt;
	mPreviousTime += dt;
	mBlendTime += dt;
	const auto& current = activeState();
	const float rate = std::max(0.f, current.rateProgram ? evaluateProgram(*current.rateProgram) : 1.f);
	require(std::isfinite(rate), "non-finite playback rate");
	mPlaybackTime += dt * rate;
	if (!current.syncGroup.empty()) {
		const auto& clock = mDefinition->ir.clips[current.clockClipIndex];
		const float duration = mSamples.at(clock.name)->duration();
		auto& phase = mPhases[current.syncGroup];
		phase += dt * rate / duration;
		phase -= std::floor(phase);
	}
	dispatchClockEvents(mPreviousPlaybackTime, mPlaybackTime, rate);
	auto pose = sampleNode(current.nodeIndex, mPlaybackTime, dt);
	applyLayerStack(pose, mPlaybackTime, dt);
	const auto& rig = mDefinition->ir.rig;
	const int root = rig.rootJoint;
	if (current.applyPoseOffset && root >= 0 && root < int(pose.numKeyframes)) {
		pose.keyframes[root].translation += mPoseOffset / rig.unitsToMeters;
	}
	applyRootMotionExtraction(pose);
	if (blendWeight() < 1 && !mPreviousState.empty()) {
		Pose previous = mPreviousPose;
		if (!mSnapshotBlend) {
			const auto savedIndex = mStateIndex;
			const auto savedName = mState;
			const float time = mTime, playback = mPlaybackTime;
			const auto previousIndex = mDefinition->ir.stateByName.at(mPreviousState);
			mStateIndex = previousIndex;
			mState = mPreviousState;
			mTime = mPreviousTime;
			mPlaybackTime = mPreviousPlaybackTime;
			const auto& previousState = activeState();
			const float previousRate = std::max(0.f, previousState.rateProgram ? evaluateProgram(*previousState.rateProgram) : 1.f);
			mPreviousPlaybackTime += dt * previousRate;
			const auto previousGroup = previousState.syncGroup;
			if (!previousGroup.empty() && previousGroup != current.syncGroup) {
				auto& phase = mPhases[previousGroup];
				const auto& clock = mDefinition->ir.clips[previousState.clockClipIndex];
				phase += dt * previousRate / mSamples.at(clock.name)->duration();
				phase -= std::floor(phase);
			}
			previous = sampleNode(previousState.nodeIndex, mPreviousPlaybackTime, dt);
			mStateIndex = savedIndex;
			mState = savedName;
			mTime = time;
			mPlaybackTime = playback;
		}
		pose = *mBlend.blend(&previous, &pose, blendWeight());
	}
	mOutput = pose;
	mEnvironment->clearTriggers();
}
void StateGraphRuntime::updateBindings(float dt) {
	for (const auto& binding : mDefinition->bindings) {
		auto value = binding.expression.evaluate([&](size_t slot) { return readExpressionSlot(slot); });
		if (binding.response > 0) {
			const float target = DATA::number(value);
			auto [it, added] = mSmoothedBindings.try_emplace(std::to_string(binding.slot), target);
			if (!added && dt > 0) {
				it->second += (target - it->second) * (1 - std::exp(-binding.response * dt));
			}
			value = it->second;
		}
		mExpressionValues.at(binding.slot) = value;
	}
}
std::vector<AnimationEventDispatch> StateGraphRuntime::consumeAnimationEvents() {
	return std::exchange(mPendingEvents, {});
}
bool StateGraphRuntime::replace(std::shared_ptr<const StateGraphDefinition> def, std::string& error) {
	try {
		require(def->skeleton->getNumJolts() == mDefinition->skeleton->getNumJolts(),
			"reload changed skeleton joint count; reload scene to replace the rig");
		for (size_t i = 0; i < def->skeleton->joints().size(); ++i) {
			require(def->skeleton->joints()[i].name == mDefinition->skeleton->joints()[i].name
					&& def->skeleton->joints()[i].parentIndex == mDefinition->skeleton->joints()[i].parentIndex,
				"reload changed skeleton joint layout; reload scene to replace the rig");
		}
		auto parameters = std::make_shared<UTILS::Environment>(*mEnvironment);
		StateGraphRuntime candidate(def, parameters);
		if (auto it = def->ir.stateByName.find(mState); it != def->ir.stateByName.end()) {
			candidate.mStateIndex = it->second;
			candidate.mState = mState;
			candidate.mTime = mTime;
			candidate.mPlaybackTime = mPlaybackTime;
			candidate.mPhases = mPhases;
			candidate.mPoseOffset = mPoseOffset;
		}
		candidate.mSmoothedBindings.clear();
		for (const auto& [name, symbol] : def->symbols) {
			const auto old = mDefinition->symbols.find(name);
			if (old != mDefinition->symbols.end()) {
				if (auto value = mSmoothedBindings.find(std::to_string(old->second.slot)); value != mSmoothedBindings.end()) {
					candidate.mSmoothedBindings[std::to_string(symbol.slot)] = value->second;
				}
			}
		}
		candidate.mSavedTimes = mSavedTimes;
		candidate.updateBindings(0);
		candidate.mOutput = candidate.sampleNode(candidate.activeState().nodeIndex, candidate.mPlaybackTime, 0);
		const auto& rig = def->ir.rig;
		const int rootJoint = rig.rootJoint;
		if (candidate.activeState().applyPoseOffset && rootJoint >= 0) {
			candidate.mOutput.keyframes[rootJoint].translation += candidate.mPoseOffset / rig.unitsToMeters;
		}
		*mEnvironment = *parameters;
		mDefinition = def;
		mSamples = std::move(candidate.mSamples);
		mState = candidate.mState;
		mStateIndex = candidate.mStateIndex;
		mTime = candidate.mTime;
		mParameterSlots = std::move(candidate.mParameterSlots);
		mExpressionValues = std::move(candidate.mExpressionValues);
		mPlaybackTime = candidate.mPlaybackTime;
		mPhases = std::move(candidate.mPhases);
		mSmoothedBindings = std::move(candidate.mSmoothedBindings);
		mSavedTimes = std::move(candidate.mSavedTimes);
		mOutput = candidate.mOutput;
		mPreviousState.clear();
		mBlendDuration = 0;
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
}
