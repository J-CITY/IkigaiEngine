#include "animation.h"

#include "animationTimelineBind.h"
#include "coreModule/ecs/components/transform.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/event.h"
#include "utilsModule/log/logger.h"

using namespace IKIGAI::ANIMATION;
using namespace IKIGAI::MATH;
using namespace IKIGAI::MATH::CURVES;

namespace {
using namespace IKIGAI;

template <class Setter, class Getter>
bool bindTransformProperty(ECS::Entity id, std::string_view nodeName, std::string trackName, Setter set, Getter get, InterpolationType interpolation,
	AnimationProperty& out) {
	auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	const auto object = scene.findObjectByID(id);
	if (!object) {
		LOG_ERROR << nodeName << ": missing object id " << id.getUniqueId();
		return false;
	}
	auto& world = object->getWorld();
	const auto& components = *world.getComponentManager();
	const auto handle = components.getComponentHandle<ECS::TransformComponent>(id);
	if (!components.resolveComponent(handle)) {
		LOG_ERROR << nodeName << ": object has no TransformComponent";
		return false;
	}
	out = IKIGAI::ANIMATION::detail::makeComponentProperty<Vector3f>(
		std::move(trackName), world.getComponentManagerWeak(), handle, set, get, interpolation);
	return true;
}

float segmentProgress(const AnimationProperty& prop, const Curve& curve, float curTime, float duration) {
	const float linearT = std::min(1.f, duration > 0.f ? curTime / duration : 1.f);
	if (prop.getInterpolationType() == InterpolationType::CUSTOM) {
		return evaluateAtTime(curve, linearT);
	}
	return getEasingOrLinear(prop.getInterpolationType())(linearT);
}

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

Vector3f lerpVector3EulerShortest(const Vector3f& from, const Vector3f& to, float progress) {
	return Vector3f(lerpEulerDegrees(from.x, to.x, progress), lerpEulerDegrees(from.y, to.y, progress), lerpEulerDegrees(from.z, to.z, progress));
}

} // namespace

void NodableAnimationNodeSequence::update(float dt) {
	if (status == Status::FINISH || childs.empty()) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	if (childs[currentId]->status == Status::FINISH) {
		currentId++;
		if (currentId >= childs.size()) {
			status = Status::FINISH;
			return;
		}
	}
	childs[currentId]->update(dt);
}

void NodableAnimationNodeSequence::clear() {
	childs.clear();
}

void NodableAnimationNodeSequence::add(std::unique_ptr<NodableAnimationNode> node) {
	childs.push_back(std::move(node));
}

void NodableAnimationNodeSimultaneous::update(float dt) {
	if (status == Status::FINISH || childs.empty()) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	bool allFinish = true;
	for (const auto& child : childs) {
		child->update(dt);
		if (child->status != Status::FINISH) {
			allFinish = false;
		}
	}

	if (allFinish) {
		status = Status::FINISH;
	}
}

void NodableAnimationNodeSimultaneous::clear() {
	childs.clear();
}

void NodableAnimationNodeSimultaneous::add(std::unique_ptr<NodableAnimationNode> node) {
	childs.push_back(std::move(node));
}

NodableAnimationNodePos::NodableAnimationNodePos(ECS::Object::Id id, Vector3f toPos, float time, InterpolationType interpolation)
	: id(id)
	, toPos(toPos)
	, time(time) {
	if (!bindTransformProperty(id, "NodableAnimationNodePos", "propPos", &ECS::TransformComponent::setLocalPosition,
			&ECS::TransformComponent::getLocalPosition, interpolation, prop)) {
		status = Status::FINISH;
		return;
	}
	fromPos = std::get<Vector3f>(prop.get());
}

void NodableAnimationNodePos::update(float dt) {
	if (status == Status::FINISH) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	curTime += dt;
	const float progress = segmentProgress(prop, curve, curTime, time);
	const auto newPos = Vector3(
		fromPos.x + progress * (toPos.x - fromPos.x), fromPos.y + progress * (toPos.y - fromPos.y), fromPos.z + progress * (toPos.z - fromPos.z));
	prop.set(newPos);
	if (progress >= 1.0f) {
		status = Status::FINISH;
	}
}

NodableAnimationNodeScale::NodableAnimationNodeScale(ECS::Object::Id id, Vector3f to, float time, InterpolationType interpolation)
	: id(id)
	, to(to)
	, time(time) {
	if (!bindTransformProperty(id, "NodableAnimationNodeScale", "propScale", &ECS::TransformComponent::setLocalScale,
			&ECS::TransformComponent::getLocalScale, interpolation, prop)) {
		status = Status::FINISH;
		return;
	}
	from = std::get<Vector3f>(prop.get());
}

void NodableAnimationNodeScale::update(float dt) {
	if (status == Status::FINISH) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	curTime += dt;
	const float progress = segmentProgress(prop, curve, curTime, time);
	const auto newScale = Vector3(from.x + progress * (to.x - from.x), from.y + progress * (to.y - from.y), from.z + progress * (to.z - from.z));
	prop.set(newScale);
	if (progress >= 1.0f) {
		status = Status::FINISH;
	}
}

NodableAnimationNodeRotate::NodableAnimationNodeRotate(ECS::Object::Id id, Vector3f to, float time, InterpolationType interpolation)
	: id(id)
	, to(to)
	, time(time) {
	if (!bindTransformProperty(id, "NodableAnimationNodeRotate", "propRotate", &ECS::TransformComponent::setLocalRotationDeg,
			&ECS::TransformComponent::getLocalRotationDeg, interpolation, prop)) {
		status = Status::FINISH;
		return;
	}
	from = std::get<Vector3f>(prop.get());
	prop.setEulerShortestPath(true);
}

void NodableAnimationNodeRotate::update(float dt) {
	if (status == Status::FINISH) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	curTime += dt;
	const float progress = segmentProgress(prop, curve, curTime, time);
	prop.set(lerpVector3EulerShortest(from, to, progress));
	if (progress >= 1.0f) {
		status = Status::FINISH;
	}
}

NodableAnimationNodeWait::NodableAnimationNodeWait(float time)
	: time(time) { }

void NodableAnimationNodeWait::update(float dt) {
	if (status == Status::FINISH) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	curTime += dt;
	if (curTime >= time) {
		status = Status::FINISH;
	}
}

NodableAnimationNodeEvent::NodableAnimationNodeEvent(const std::string& eventId, float time)
	: time(time)
	, eventId(eventId) { }

void NodableAnimationNodeEvent::update(float dt) {
	if (status == Status::FINISH) {
		return;
	}
	if (status == Status::NOT_START) {
		status = Status::PROGRESS;
	}

	curTime += dt;
	if (curTime >= time) {
		RESOURCES::ServiceManager::Get<EVENT::EventBroadcaster>().run(eventId, EVENT::EventBroadcaster::Payload {});
		status = Status::FINISH;
	}
}

void NodableAnimation::update(float dt) {
	if (status == Status::PAUSE || status == Status::STOP) {
		return;
	}
	if (currentAnim >= static_cast<int>(animations.size())) {
		status = Status::STOP;
		return;
	}
	animations.at(currentAnim)->update(dt);
	if (animations.at(currentAnim)->status == NodableAnimationNode::Status::FINISH) {
		currentAnim++;
	}
}

void NodableAnimation::stop() {
	status = Status::STOP;
	if (currentAnim < static_cast<int>(animations.size())) {
		animations.at(currentAnim)->status = NodableAnimationNode::Status::NOT_START;
	}
	currentAnim = 0;
}

void NodableAnimation::play() {
	status = Status::PLAY;
}

void NodableAnimation::pause() {
	status = Status::PAUSE;
}
