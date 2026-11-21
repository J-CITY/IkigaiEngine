#include "stateGraphRuntime.h"
#include "characterAnimation.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace IKIGAI::SKELETON {
namespace {
	void require(bool valid, const std::string& message) {
		if (!valid) {
			throw std::runtime_error("State graph: " + message);
		}
	}
}

StateGraphRuntime::StateGraphRuntime(std::shared_ptr<const StateGraphDefinition> def, std::shared_ptr<UTILS::Environment> env)
	: mDefinition(std::move(def))
	, mEnvironment(std::move(env))
	, mBlend(mDefinition->skeleton) {
	mParameterSlots.reserve(mDefinition->parameters.size());
	for (const auto& parameter : mDefinition->parameters) {
		mEnvironment->declare(parameter.name, parameter.defaultValue, parameter.trigger);
		mParameterSlots.push_back(mEnvironment->slot(parameter.name));
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
void StateGraphRuntime::reset() {
	for (size_t i = 0; i < mDefinition->parameters.size(); ++i) {
		mEnvironment->set(mParameterSlots[i], mDefinition->parameters[i].defaultValue);
	}
	mPhases.clear();
	mSmoothedBindings.clear();
	mSavedTimes.clear();
	mState.clear();
	mPreviousState.clear();
	mOutput = {};
	mPoseOffset = {};
	mLastTransition.clear();
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
		mLastTransition = selected->displayText;
		const auto it = mDefinition->ir.stateByName.find(selected->target);
		require(it != mDefinition->ir.stateByName.end(), "transition target missing from IR");
		enter(it->second, selected->blend);
		for (const auto slot : selected->consumeSlots) {
			mEnvironment->set(mParameterSlots.at(slot), false);
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
