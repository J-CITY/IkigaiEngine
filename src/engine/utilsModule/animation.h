#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "coreModule/ecs/object.h"
#include "mathModule/curves.h"
#include "sceneModule/sceneManager.h"
#include "weakPtr.h"

namespace IKIGAI::ANIMATION {
namespace FUNC = MATH::CURVES::FUNC;
using MATH::CURVES::Curve;
using MATH::CURVES::getEasing;
using MATH::CURVES::getEasingOrLinear;
using MATH::CURVES::InterpolationType;
using MATH::CURVES::makeUnitBezierCurve;
using PropType = std::variant<float, int, bool, std::string, MATH::Vector4f, MATH::Vector3f, MATH::Vector2f>;

class AnimationProperty {
	std::string name;
	InterpolationType interpolationType;
	std::function<void(PropType)> _set;
	std::function<PropType()> _get;
	bool eulerShortestPath = false;
	bool timelineEventTrack = false;

public:
	AnimationProperty() = default;
	AnimationProperty(
		std::string name, std::function<void(PropType)> set, std::function<PropType()> get, InterpolationType type = InterpolationType::LINEAR);
	void set(PropType val);
	PropType get();
	const std::string& getName() const;
	InterpolationType getInterpolationType() const;
	void setEulerShortestPath(bool value);
	bool getEulerShortestPath() const;
	void setTimelineEventTrack(bool value);
	bool isTimelineEventTrack() const;
};

struct TimelineAnimationFrameDescriptor {
	int frame = 0;
	PropType value;

	InterpolationType curveType = InterpolationType::LINEAR;
	Curve curve;

	template <class Context> constexpr static auto serde(Context& context, TimelineAnimationFrameDescriptor& value) {
		using Self = TimelineAnimationFrameDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::frame, "Frame")
			.field(&Self::value, "Value")
			.field(&Self::curveType, "CurveType")
			.field(&Self::curve, "Curve");
	}
};

struct TimelineAnimationTrackDescriptor {
	std::string objectName;
	int objectId = -1;
	std::string componentName;
	std::string propertyName;

	template <class Context> constexpr static auto serde(Context& context, TimelineAnimationTrackDescriptor& value) {
		using Self = TimelineAnimationTrackDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::objectName, "ObjectName")
			.field(&Self::objectId, "ObjectId")
			.field(&Self::componentName, "ComponentName")
			.field(&Self::propertyName, "PropertyName");
	}
};

struct TimelineAnimationDescriptor {
	bool isLooped = false;
	float framesCount = 100;
	float FPS = 60;
	// track name -> track info
	std::map<std::string, TimelineAnimationTrackDescriptor> tracks;

	// track name -> array of frames
	std::map<std::string, std::vector<TimelineAnimationFrameDescriptor>> trackFrames;

	std::string pathResource;

	template <class Context> constexpr static auto serde(Context& context, TimelineAnimationDescriptor& value) {
		using Self = TimelineAnimationDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::isLooped, "IsLooped")
			.field(&Self::framesCount, "FramesCount")
			.field(&Self::FPS, "FPS")
			.field(&Self::tracks, "Tracks")
			.field(&Self::trackFrames, "TrackFrames");
	}
};

class AnimationBase
//: public UTILS::ControlBlockHandler TODO: add component
{
public:
	AnimationBase() = default;
	virtual ~AnimationBase() = default;

	enum class Status { PLAY, PAUSE, STOP };
	virtual void update(float dt) = 0;
	virtual void stop() = 0;
	virtual void play() = 0;
	virtual void pause() = 0;
	const Status& getStatus() const;

protected:
	bool isLooped = false;
	Status animStatus = Status::STOP;
};

class Animation : public AnimationBase {
	friend class Builder;

public:
	int FPS = 30;
	int framesCount = 50;
	// Playback clock in seconds (authoring grid still uses framesCount / FPS).
	double curTime = 0.0;

	std::map<int, std::map<std::string, PropType>> frames;
	std::map<std::string, AnimationProperty> props;
	std::map<std::string, std::map<int, Curve>> curves;
	std::map<std::string, std::map<int, InterpolationType>> segmentInterpolation;
	std::map<std::string, TimelineAnimationTrackDescriptor> trackDescriptors;
	bool isInit = false;
	std::map<std::string, std::vector<int>> fastAccess;
	std::map<std::string, PropType> curState;

public:
	struct Builder {
	private:
		std::unique_ptr<Animation> anim;

	public:
		Builder(unsigned int framesCount, unsigned int fps, bool isLooped);
		Builder& addKeyFrame(int frame, std::map<std::string, PropType> prop);
		Builder& addKeyFrameMerge(int frame, std::map<std::string, PropType> prop);
		Builder& addProperty(AnimationProperty p);
		std::unique_ptr<Animation> build();
	};
	// private:
	Animation(unsigned int frameCount, unsigned int fps, bool isLooped);
	void setFrameCount(unsigned _frameCount);
	void setFPS(unsigned fps);
	void setLooped(bool b);
	unsigned getFrameCount() const {
		return framesCount;
	};
	unsigned getFPS() const {
		return FPS;
	};
	bool getLooped() const {
		return isLooped;
	};
	void init();

	float linearFunc(float x0, float x1, float progress);
	int linearFunc(int x0, int x1, float progress);
	bool linearFunc(bool x0, bool x1, float progress);

	void moveFrameInTrack(const std::string& trackName, int oldFrame, int newFrame);

	void addProperty(AnimationProperty p);
	void delProperty(const std::string& name);
	void addKeyFrame(int frame, std::map<std::string, PropType> prop);
	void delKeyFrameProp(int frame, std::map<std::string, PropType> prop);
	void delKeyFrameProp(int frame, const std::string& propName);
	void addKeyFrameMerge(int frame, std::map<std::string, PropType> prop);
	void setSegmentInterpolation(const std::string& track, int frame, InterpolationType type);

	bool hasFrameForProperty(int frame, const std::string& name);
	double getDurationSeconds() const;
	double frameToTime(int frame) const;

	static std::unique_ptr<Animation> createFromDescriptor(const TimelineAnimationDescriptor& desc, ECS::World& world);

public:
	void update(float dt) override;
	void play() override;
	void stop() override;
	void pause() override;
};

class StateMachine {
	friend class Builder;

public:
	enum class Status {
		PLAY,
		PAUSE,
		STOP,
	};

	using StateType = unsigned int;
	using Condition = std::function<bool()>;
	struct Edge {
		StateType type;
		Condition condition;
	};
	using Edges = std::vector<Edge>;
	using StateGraphMap = std::map<StateType, Edges>;

	struct Builder {
	private:
		StateGraphMap stateGraph;
		std::map<StateType, std::vector<std::unique_ptr<AnimationBase>>> states;
		StateType start = 0;

	public:
		Builder();
		Builder& addEdge(StateType from, StateType to, std::function<bool()> act);
		Builder& addState(StateType newState, std::unique_ptr<AnimationBase> anim);
		Builder& setStartState(StateType state);
		StateMachine build();
	};

	void setStartState(StateType state);
	void setState(StateType state);
	void update(float dt);
	void play();
	void stop();
	void pause();
	StateType getCurState() const;
	StateType getStartState() const;
	Status getStatus() const;

private:
	StateMachine();

	StateGraphMap stateGraph;
	std::map<StateType, std::vector<std::unique_ptr<AnimationBase>>> states;
	StateType currentState;
	StateType startState;
	Status status = Status::STOP;
};

// class NodableAnimation : public AnimationBase {
//	std::unique_ptr<NodableAnimationNode> root;
// public:
//	void update(float dt) override {
//		root->update(dt);
//	}
//	void stop() override;
//	void play() override;
//	void pause() override;
// };

class NodableAnimationNode {
public:
	enum class Status {
		NOT_START,
		PROGRESS,
		FINISH,
	};
	NodableAnimationNode() = default;
	virtual ~NodableAnimationNode() = default;
	Status status = Status::NOT_START;

	virtual void update(float dt) = 0;
};

class NodableAnimationNodeSequence : public NodableAnimationNode {
	std::vector<std::unique_ptr<NodableAnimationNode>> childs;
	size_t currentId = 0;

public:
	void update(float dt) override;
	void clear();
	void add(std::unique_ptr<NodableAnimationNode> node);
};

class NodableAnimationNodeSimultaneous : public NodableAnimationNode {
public:
	std::vector<std::unique_ptr<NodableAnimationNode>> childs;

public:
	void update(float dt) override;
	void clear();
	void add(std::unique_ptr<NodableAnimationNode> node);
};

class NodableAnimationNodePos : public NodableAnimationNode {
public:
	ECS::Object::Id id;
	MATH::Vector3f toPos {};

	MATH::Vector3f fromPos {};
	float time = 0.0f;
	float curTime = 0.0f;

	Curve curve;
	AnimationProperty prop;

	NodableAnimationNodePos(ECS::Object::Id id, MATH::Vector3f toPos, float time, InterpolationType interpolation = InterpolationType::LINEAR);
	void update(float dt) override;
};

class NodableAnimationNodeScale : public NodableAnimationNode {
public:
	ECS::Object::Id id;
	MATH::Vector3f to {};

	MATH::Vector3f from {};
	float time = 0.0f;
	float curTime = 0.0f;

	Curve curve;
	AnimationProperty prop;

	NodableAnimationNodeScale(ECS::Object::Id id, MATH::Vector3f to, float time, InterpolationType interpolation = InterpolationType::LINEAR);
	void update(float dt) override;
};

class NodableAnimationNodeRotate : public NodableAnimationNode {
public:
	ECS::Object::Id id;
	MATH::Vector3f to {};

	MATH::Vector3f from {};
	float time = 0.0f;
	float curTime = 0.0f;

	Curve curve;
	AnimationProperty prop;

	NodableAnimationNodeRotate(ECS::Object::Id id, MATH::Vector3f to, float time, InterpolationType interpolation = InterpolationType::LINEAR);
	void update(float dt) override;
};

class NodableAnimationNodeWait : public NodableAnimationNode {
public:
	float time = 0.0f;
	float curTime = 0.0f;

	explicit NodableAnimationNodeWait(float time);
	void update(float dt) override;
};

class NodableAnimationNodeEvent : public NodableAnimationNode {
public:
	float time = 0.0f;
	float curTime = 0.0f;
	std::string eventId;

	NodableAnimationNodeEvent(const std::string& eventId, float time);
	void update(float dt) override;
};

class NodableAnimation : public AnimationBase {
public:
	NodableAnimation() = default;
	virtual ~NodableAnimation() = default;
	Status status = Status::STOP;
	std::vector<std::unique_ptr<NodableAnimationNode>> animations;
	int currentAnim = 0;
	void update(float dt) override;
	void stop() override;
	void play() override;
	void pause() override;
};

}
