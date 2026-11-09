#include "animation.h"

#include "animationTimelineBind.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/event.h"
#include "utilsModule/log/logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace IKIGAI::ANIMATION;
using namespace IKIGAI::MATH;
using namespace IKIGAI::MATH::CURVES;

AnimationProperty::AnimationProperty(std::string name, std::function<void(PropType)> set, std::function<PropType()> get, InterpolationType type)
	: name(name)
	, _set(std::move(set))
	, _get(std::move(get))
	, interpolationType(type) { }

void AnimationProperty::set(PropType val) {
	_set(val);
};
PropType AnimationProperty::get() {
	return _get();
};
const std::string& AnimationProperty::getName() const {
	return name;
}
InterpolationType AnimationProperty::getInterpolationType() const {
	return interpolationType;
}

void AnimationProperty::setEulerShortestPath(bool value) {
	eulerShortestPath = value;
}

bool AnimationProperty::getEulerShortestPath() const {
	return eulerShortestPath;
}

void AnimationProperty::setTimelineEventTrack(bool value) {
	timelineEventTrack = value;
}

bool AnimationProperty::isTimelineEventTrack() const {
	return timelineEventTrack;
}

const AnimationBase::Status& AnimationBase::getStatus() const {
	return animStatus;
}

Animation::Builder::Builder(unsigned int framesCount, unsigned int fps, bool isLooped)
	: anim(new Animation(framesCount, fps, isLooped)) { }

Animation::Builder& Animation::Builder::addKeyFrame(int frame, std::map<std::string, PropType> prop) {
	anim->addKeyFrame(frame, prop);
	return *this;
}

Animation::Builder& Animation::Builder::addKeyFrameMerge(int frame, std::map<std::string, PropType> prop) {
	anim->addKeyFrameMerge(frame, prop);
	return *this;
}

Animation::Builder& Animation::Builder::addProperty(AnimationProperty p) {
	anim->addProperty(std::move(p));
	return *this;
}

std::unique_ptr<Animation> Animation::Builder::build() {
	return std::move(anim);
}

Animation::Animation(unsigned int frameCount, unsigned int fps, bool isLooped)
	: AnimationBase()
	, framesCount(static_cast<int>(frameCount))
	, FPS(static_cast<int>(fps)) {
	this->isLooped = isLooped;
}

void Animation::setFrameCount(unsigned int _frameCount) {
	framesCount = _frameCount;
}

void Animation::setFPS(unsigned int fps) {
	FPS = fps;
}

void Animation::setLooped(bool b) {
	isLooped = b;
}

void Animation::init() {
	isInit = true;
	fastAccess.clear();
	for (auto& s : frames) {
		for (auto& p : s.second) {
			fastAccess[p.first].push_back(s.first);
		}
	}
	for (auto& v : fastAccess) {
		std::sort(v.second.begin(), v.second.end());
	}
}

float Animation::linearFunc(float x0, float x1, float progress) {
	if (progress > 1.f) {
		progress = 1.0f;
	}
	return static_cast<float>(x0 + (x1 - x0) * progress);
}

int Animation::linearFunc(int x0, int x1, float progress) {
	if (progress > 1.f) {
		progress = 1.0f;
	}
	return static_cast<int>(x0 + (x1 - x0) * progress);
}

bool Animation::linearFunc(bool x0, bool x1, float progress) {
	if (progress > 1.f) {
		progress = 1.0f;
	}
	return progress == 1.0f ? x1 : x0;
}

void Animation::moveFrameInTrack(const std::string& trackName, int oldFrame, int newFrame) {
	if (!frames.contains(oldFrame)) {
		return;
	}
	if (!frames[oldFrame].contains(trackName)) {
		return;
	}

	auto prop = frames[oldFrame][trackName];
	auto curve = curves[trackName][oldFrame];
	// Delete old
	frames[oldFrame].erase(trackName);
	curves[trackName].erase(oldFrame);

	// Add to new
	if (frames[newFrame].contains(trackName)) {
		if (newFrame > oldFrame) {
			if (newFrame < framesCount - 1) {
				newFrame++;
			}
			else {
				newFrame = oldFrame;
			}
		}
		else {
			if (newFrame > 0) {
				newFrame--;
			}
			else {
				newFrame = oldFrame;
			}
		}
	}
	curves[trackName][newFrame] = curve;
	frames[newFrame].insert_or_assign(trackName, prop);

	// frames.emplace(frame, prop);
	init();
}

void Animation::addProperty(AnimationProperty p) {
	auto n = p.getName();
	props.emplace(n, std::move(p));
	isInit = false;
}

void Animation::delProperty(const std::string& name) {
	props.erase(name);
	curves.erase(name);
	isInit = false;
}

void Animation::addKeyFrame(int frame, std::map<std::string, PropType> prop) {
	frames.emplace(frame, prop);

	for (auto& p : prop) {
		// int startFrame = -1;
		// for (auto& e : fastAccess[p.first]) {
		//	if (e < frame) {
		//		continue;
		//	}
		//	startFrame = e;
		//	break;
		// }
		// if (frame == 0 && !curves[p.first].empty() && curves[p.first][0].mLeft.x == 0.0f) {
		//	curves[p.first].erase(curves[p.first].begin());
		// }

		curves[p.first][frame] = makeUnitBezierCurve();
		segmentInterpolation[p.first][frame] = InterpolationType::LINEAR;

		// std::sort(curves[p.first].begin(), curves[p.first].end(), [](const Curve& a, const Curve& b) {
		//	return a.mLeft.x < b.mLeft.x;
		// });
		//
		// auto& _curves = curves[p.first];
		// for (int i = 0; i < _curves.size()-1; ++i) {
		//	_curves[i].mRight = _curves[i + 1].mLeft;
		// }
		//
		// if (!_curves.empty() && _curves[0].mLeft.x != 0.0f) {
		//	_curves.emplace(_curves.begin(), Curve{
		//		{static_cast<float>(0.0f), 0.5f},
		//		_curves[0].mLeft,
		//		{0.5f, 0.0f},
		//		{-1.0f, 0.0f}
		//	});
		// }
	}

	init();
}

void Animation::delKeyFrameProp(int frame, std::map<std::string, PropType> prop) {
	if (!frames.contains(frame)) {
		return;
	}
	for (auto& e : prop) {
		frames[frame].erase(e.first);
		curves[e.first].erase(frame);
		// std::erase_if(curves[e.first], [frame](const Curve& x) { return static_cast<int>(x.mLeft.x) == frame; });
	}

	// frames.emplace(frame, prop);
	init();

	for (auto& e : prop) {
		if (fastAccess[e.first].empty()) {
			curves.erase(e.first);
		}
	}
}

void Animation::delKeyFrameProp(int frame, const std::string& propName) {
	if (!frames.contains(frame)) {
		return;
	}
	frames[frame].erase(propName);
	if (frames[frame].empty()) {
		frames.erase(frame);
	}

	curves[propName].erase(frame);
	// std::erase_if(curves[propName], [frame](const Curve& x) { return static_cast<int>(x.mLeft.x) == frame; });

	init();

	if (fastAccess[propName].empty()) {
		curves.erase(propName);
	}
}

void Animation::addKeyFrameMerge(int frame, std::map<std::string, PropType> prop) {
	if (frames.find(frame) == frames.end()) {
		frames.emplace(frame, prop);
	}
	else {
		for (auto& p : prop) {
			frames.at(frame).insert_or_assign(p.first, p.second);
		}
	}

	for (auto& p : prop) {
		// if (frame == 0 && !curves[p.first].empty() && curves[p.first][0].mLeft.x == 0.0f) {
		//	curves[p.first].erase(curves[p.first].begin());
		// }

		curves[p.first][frame] = makeUnitBezierCurve();
		segmentInterpolation[p.first][frame] = InterpolationType::LINEAR;

		// std::sort(curves[p.first].begin(), curves[p.first].end(), [](const Curve& a, const Curve& b) {
		//	return a.mLeft.x < b.mLeft.x;
		// });
		//
		// auto& _curves = curves[p.first];
		// for (int i = 0; i < _curves.size() - 1; ++i) {
		//	_curves[i].mRight = _curves[i + 1].mLeft;
		// }
		//
		// if (!_curves.empty() && _curves[0].mLeft.x != 0.0f) {
		//	_curves.emplace(_curves.begin(), Curve{
		//		{static_cast<float>(0.0f), 0.5f},
		//		_curves[0].mLeft,
		//		{0.5f, 0.0f},
		//		{-1.0f, 0.0f}
		//	});
		// }
	}
	init();
}

bool Animation::hasFrameForProperty(int frame, const std::string& name) {
	auto& vec = fastAccess[name];
	return std::find(vec.begin(), vec.end(), frame) != vec.end();
}

void Animation::setSegmentInterpolation(const std::string& track, int frame, InterpolationType type) {
	segmentInterpolation[track][frame] = type;
}

double Animation::getDurationSeconds() const {
	if (FPS <= 0) {
		return 0.0;
	}
	return static_cast<double>(framesCount) / static_cast<double>(FPS);
}

double Animation::frameToTime(int frame) const {
	if (FPS <= 0) {
		return 0.0;
	}
	return static_cast<double>(frame) / static_cast<double>(FPS);
}

std::unique_ptr<Animation> Animation::createFromDescriptor(const TimelineAnimationDescriptor& desc, ECS::World& world) {
	auto animation = std::make_unique<Animation>(static_cast<unsigned int>(desc.framesCount), static_cast<unsigned int>(desc.FPS), desc.isLooped);
	animation->trackDescriptors = desc.tracks;

	for (const auto& track : desc.tracks) {
		AnimationProperty property;
		// Event tracks have no target component and can carry the unset object ID.
		const int objectId = track.second.componentName == "EVENT" ? 0 : track.second.objectId;
		if (objectId < 0
			|| !tryBindTimelineProperty(
				world, ECS::Entity(ECS::Entity::ID(objectId)), track.second.componentName, track.second.propertyName, property, track.first)) {
			LOG_WARNING << "Property animation: skipped track '" << track.first << "' (bind failed for " << track.second.componentName << '.'
						<< track.second.propertyName << ')';
			continue;
		}
		animation->addProperty(std::move(property));
	}

	for (const auto& trackFrames : desc.trackFrames) {
		const auto trackName = trackFrames.first;
		if (!animation->props.contains(trackName)) {
			continue;
		}
		for (const auto& frame : trackFrames.second) {
			if (!animation->hasFrameForProperty(frame.frame, trackName)) {
				std::map<std::string, PropType> keyed;
				keyed[trackName] = frame.value;
				animation->addKeyFrameMerge(frame.frame, keyed);
			}
			animation->curves[trackName][frame.frame] = frame.curve;
			animation->setSegmentInterpolation(trackName, frame.frame, frame.curveType);
		}
	}

	return animation;
}

template <class... Ts> struct overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace {

float lerpEulerDegrees(float from, float to, float progress) {
	float delta = to - from;
	while (delta > 180.f) {
		delta -= 360.f;
	}
	while (delta < -180.f) {
		delta += 360.f;
	}
	return from + delta * progress;
}

IKIGAI::MATH::Vector3f lerpVector3(const IKIGAI::MATH::Vector3f& from, const IKIGAI::MATH::Vector3f& to, float progress, bool eulerShortest) {
	if (eulerShortest) {
		return IKIGAI::MATH::Vector3f(
			lerpEulerDegrees(from.x, to.x, progress), lerpEulerDegrees(from.y, to.y, progress), lerpEulerDegrees(from.z, to.z, progress));
	}
	return IKIGAI::MATH::Vector3f(from.x + (to.x - from.x) * progress, from.y + (to.y - from.y) * progress, from.z + (to.z - from.z) * progress);
}

InterpolationType segmentTypeFor(const Animation& animation, const std::string& track, int startFrame, const AnimationProperty& property) {
	if (const auto trackIt = animation.segmentInterpolation.find(track); trackIt != animation.segmentInterpolation.end()) {
		if (const auto frameIt = trackIt->second.find(startFrame); frameIt != trackIt->second.end()) {
			return frameIt->second;
		}
	}
	return property.getInterpolationType();
}

float applySegmentEasing(InterpolationType type, const Curve& curve, float segmentTime) {
	if (type == InterpolationType::CUSTOM) {
		return evaluateAtTime(curve, segmentTime);
	}
	return getEasingOrLinear(type)(segmentTime);
}

} // namespace

void Animation::update(float dt) {
	if (animStatus == AnimationBase::Status::PAUSE || animStatus == AnimationBase::Status::STOP) {
		return;
	}
	if (!isInit) {
		init();
	}

	const double prevTime = curTime;
	curTime += static_cast<double>(dt);
	const double duration = getDurationSeconds();

	auto dispatchTimelineEvents = [&](double rangeStart, double rangeEnd) {
		if (rangeEnd <= rangeStart) {
			return;
		}
		for (const auto& property : props) {
			if (!property.second.isTimelineEventTrack()) {
				continue;
			}
			const auto keysIt = fastAccess.find(property.first);
			if (keysIt == fastAccess.end()) {
				continue;
			}
			for (const int frame : keysIt->second) {
				const double keyTime = frameToTime(frame);
				if (keyTime > rangeStart && keyTime <= rangeEnd) {
					if (const auto frameIt = frames.find(frame); frameIt != frames.end()) {
						if (const auto valueIt = frameIt->second.find(property.first); valueIt != frameIt->second.end()) {
							if (const auto* eventName = std::get_if<std::string>(&valueIt->second)) {
								RESOURCES::ServiceManager::Get<EVENT::EventBroadcaster>().run(*eventName, EVENT::EventBroadcaster::Payload {});
							}
						}
					}
				}
			}
		}
	};

	if (duration > 0.0 && curTime > duration) {
		if (isLooped) {
			dispatchTimelineEvents(prevTime, duration);
			while (curTime > duration) {
				curTime -= duration;
			}
			dispatchTimelineEvents(0.0, curTime);
		}
		else {
			curTime = duration;
			animStatus = AnimationBase::Status::STOP;
			dispatchTimelineEvents(prevTime, duration);
		}
	}
	else {
		dispatchTimelineEvents(prevTime, curTime);
	}

	curState.clear();
	for (auto& property : props) {
		if (property.second.isTimelineEventTrack()) {
			continue;
		}
		const auto keysIt = fastAccess.find(property.first);
		if (keysIt == fastAccess.end() || keysIt->second.size() < 2) {
			continue;
		}
		const auto& keys = keysIt->second;
		for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
			const int startFrame = keys[i];
			const int endFrame = keys[i + 1];
			const double startTime = frameToTime(startFrame);
			const double endTime = frameToTime(endFrame);
			if (curTime < startTime || curTime > endTime) {
				continue;
			}
			const double segmentDuration = endTime - startTime;
			float progress = segmentDuration > 0.0 ? static_cast<float>((curTime - startTime) / segmentDuration) : 1.f;
			progress = std::clamp(progress, 0.f, 1.f);

			const auto segmentType = segmentTypeFor(*this, property.first, startFrame, property.second);
			const auto& curve = curves.at(property.first).at(startFrame);
			progress = applySegmentEasing(segmentType, curve, progress);

			std::visit(
				overloaded { [this, &property, startFrame, endFrame, progress](const float&) {
								const auto* val1 = std::get_if<float>(&frames.at(startFrame).at(property.first));
								const auto* val2 = std::get_if<float>(&frames.at(endFrame).at(property.first));
								if (val1 && val2) {
									curState[property.first] = linearFunc(*val1, *val2, progress);
								}
							},
				[this, &property, startFrame, endFrame, progress](const int&) {
					const auto* val1 = std::get_if<int>(&frames.at(startFrame).at(property.first));
					const auto* val2 = std::get_if<int>(&frames.at(endFrame).at(property.first));
					if (val1 && val2) {
						curState[property.first] = linearFunc(*val1, *val2, progress);
					}
				},
				[this, &property, startFrame, endFrame, progress](const bool&) {
					const auto* val1 = std::get_if<bool>(&frames.at(startFrame).at(property.first));
					const auto* val2 = std::get_if<bool>(&frames.at(endFrame).at(property.first));
					if (val1 && val2) {
						curState[property.first] = linearFunc(*val1, *val2, progress);
					}
				},
				[this, &property, startFrame, endFrame, progress](const MATH::Vector2f&) {
					const auto* val1 = std::get_if<MATH::Vector2f>(&frames.at(startFrame).at(property.first));
					const auto* val2 = std::get_if<MATH::Vector2f>(&frames.at(endFrame).at(property.first));
					if (val1 && val2) {
						curState[property.first] = MATH::Vector2f(linearFunc(val1->x, val2->x, progress), linearFunc(val1->y, val2->y, progress));
					}
				},
				[this, &property, startFrame, endFrame, progress](const MATH::Vector3f&) {
					const auto* val1 = std::get_if<MATH::Vector3f>(&frames.at(startFrame).at(property.first));
					const auto* val2 = std::get_if<MATH::Vector3f>(&frames.at(endFrame).at(property.first));
					if (val1 && val2) {
						curState[property.first] = lerpVector3(*val1, *val2, progress, property.second.getEulerShortestPath());
					}
				},
				[this, &property, startFrame, endFrame, progress](const MATH::Vector4f&) {
					const auto* val1 = std::get_if<MATH::Vector4f>(&frames.at(startFrame).at(property.first));
					const auto* val2 = std::get_if<MATH::Vector4f>(&frames.at(endFrame).at(property.first));
					if (val1 && val2) {
						curState[property.first] = MATH::Vector4f(linearFunc(val1->x, val2->x, progress), linearFunc(val1->y, val2->y, progress),
							linearFunc(val1->z, val2->z, progress), linearFunc(val1->w, val2->w, progress));
					}
				},
				[](const std::string&) {}, [](const auto&) {} },
				frames.at(startFrame).at(property.first));
			break;
		}
	}

	for (auto& value : curState) {
		props.at(value.first).set(value.second);
	}
}

void Animation::play() {
	animStatus = AnimationBase::Status::PLAY;
}

void Animation::stop() {
	curTime = 0.0;
	animStatus = AnimationBase::Status::STOP;
}

void Animation::pause() {
	animStatus = AnimationBase::Status::PAUSE;
}

// State Machine

StateMachine::Builder::Builder() = default;

StateMachine::Builder& StateMachine::Builder::addState(StateType newState, std::unique_ptr<AnimationBase> anim) {
	if (states.find(newState) == states.end()) {
		states[newState] = std::vector<std::unique_ptr<AnimationBase>>();
	}
	states.at(newState).push_back(std::move(anim));
	return *this;
}
StateMachine::Builder& StateMachine::Builder::setStartState(StateType state) {
	start = state;
	return *this;
}

StateMachine::Builder& StateMachine::Builder::addEdge(StateType from, StateType to, std::function<bool()> act) {
	if (stateGraph.find(from) == stateGraph.end()) {
		stateGraph[from] = Edges();
	}
	stateGraph[from].push_back(Edge { to, act });
	return *this;
}

StateMachine StateMachine::Builder::build() {
	StateMachine stateMachine;
	stateMachine.states = std::move(states);
	stateMachine.stateGraph = std::move(stateGraph);
	stateMachine.setStartState(start);
	return std::move(stateMachine);
}

StateMachine::StateMachine() = default;

void StateMachine::setStartState(StateType state) {
	startState = state;
}

void StateMachine::setState(StateType state) {
	currentState = state;
	for (auto& anim : states.at(currentState)) {
		anim->play();
	}
}

void StateMachine::update(float dt) {
	if (status != Status::PLAY) {
		return;
	}
	for (auto& anim : states.at(currentState)) {
		anim->update(dt);
	}
	if (stateGraph.find(currentState) == stateGraph.end()) {
		return;
	}
	for (auto& s : stateGraph.at(currentState)) {
		if (s.condition()) {
			for (auto& anim : states.at(currentState)) {
				anim->stop();
			}
			currentState = s.type;
			for (auto& anim : states.at(currentState)) {
				anim->play();
			}
			break;
		}
	}
}

void StateMachine::play() {
	currentState = startState;
	status = Status::PLAY;
}

void StateMachine::stop() {
	status = Status::STOP;
	currentState = startState;
}

void StateMachine::pause() {
	status = Status::PAUSE;
}

StateMachine::StateType StateMachine::getCurState() const {
	return currentState;
}

StateMachine::StateType StateMachine::getStartState() const {
	return startState;
}

StateMachine::Status StateMachine::getStatus() const {
	return status;
}
