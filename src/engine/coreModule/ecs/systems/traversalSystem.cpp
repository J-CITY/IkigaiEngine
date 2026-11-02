#include "traversalSystem.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/traversalComponent.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/skeletonAnimationManager.h"
#include "resourceModule/skeletonManager.h"
#include "sceneModule/scene.h"
#include <serdepp/adaptor/nlohmann_json.hpp>
#include "coreModule/testHooks.h"
#include <set>
namespace IKIGAI::ECS {
using namespace PHYSICS::CHARACTER;
using PHYSICS::Vec3;
namespace Motion = SKELETON::CHARACTER;
namespace {
	constexpr float radians = .01745329252f;
	Vec3 rotate(Vec3 value, float heading) {
		return MATH::QuaternionF(Vec3 { 0, heading, 0 }) * value;
	}
	bool pathClear(const PHYSICS::World& world, Capsule capsule, Vec3 end) {
		return !sweep(world, capsule, end - capsule.feet);
	}
}
void TraversalSystem::prepare(TraversalComponent& t) {
	if (t.prepared) {
		return;
	}
	auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(t.settings.Actions);
	auto json = nlohmann::json::parse(file->readStr());
	const auto doc = serde::deserialize<RESOURCES::TraversalActionsDescriptor>(json);
	if (doc.Version != 1) {
		throw std::runtime_error("Unsupported traversal actions version");
	}
	t.skeleton = RESOURCES::ServiceManager::Get<RESOURCES::SkeletonLoader>().loadResource(doc.SkeletonPath);
	const auto& rig = doc.Rig;
	t.rig.root = rig.Root;
	t.rig.feet = rig.Feet;
	t.rig.hands = rig.Hands;
	t.rig.unitsToMeters = rig.UnitsToMeters;
	t.rig.rootHeight = rig.RootHeight * t.rig.unitsToMeters;
	t.rig.soleOffset = rig.SoleOffset;
	const DATA::Symbols facts = { { "ObstacleHeight", { 0, DATA::ValueType::Float } }, { "Vault", { 1, DATA::ValueType::Bool } },
		{ "Sprint", { 2, DATA::ValueType::Bool } } };
	std::vector<TraversalComponent::Candidate> candidate;
	std::set<int> ids;
	for (const auto& a : doc.Actions) {
		const int id = a.Id;
		if (id <= 0 || !ids.insert(id).second) {
			throw std::runtime_error("Invalid/duplicate traversal action ID");
		}
		DATA::ExpressionDescriptor unconditional;
		unconditional.Constant.Type = DATA::ValueType::Bool;
		unconditional.Constant.Bool = true;
		auto condition = DATA::Expression::compile(a.When.value_or(unconditional), facts);
		if (condition.type() != DATA::ValueType::Bool) {
			throw std::runtime_error("Traversal condition requires Bool");
		}
		auto clip = RESOURCES::SkeletonAnimationLoader::CreateFromResource(a.Clip, t.skeleton);
		const float rate = a.Rate;
		const auto metadata = a.Motion;
		const float plant = metadata.Plant, release = metadata.Release;
		if (!clip || !std::isfinite(rate) || rate <= 0 || plant < 0 || plant >= release || release > 1) {
			throw std::runtime_error("Invalid traversal action");
		}
		candidate.push_back({ id, a.Priority, std::move(condition), std::move(clip), metadata, rate });
	}
	t.candidates = std::move(candidate);
	t.prepared = true;
}
bool TraversalSystem::tryStart(const PHYSICS::World& world, CharacterMotorComponent& m, TraversalComponent& t, Vec3 direction) {
	prepare(t);
	if (!m.motor.grounded || length(direction) < .1f) {
		return false;
	}
	const auto plan = planTraversal(world, m.motor.capsule, direction);
	if (!plan) {
		return false;
	}
	t.traversalStart = plan.start;
	t.traversalRaised = plan.raised;
	t.traversalEnd = plan.end;
	t.ledge = plan.ledge;
	t.traversalObstacle = plan.obstacle;
	t.obstacleHeight = t.ledge.y - plan.start.y;
	const TraversalComponent::Candidate* selected = nullptr;
	for (const auto& c : t.candidates) {
		if ((!selected || c.priority > selected->priority) && DATA::boolean(c.condition.evaluate([&](size_t i) -> DATA::Value {
				if (i == 0) {
					return t.obstacleHeight;
				}
				if (i == 1) {
					return plan.vault;
				}
				return m.sprint;
			}))) {
			selected = &c;
		}
	}
	if (!selected) {
		return false;
	}
	auto sample = std::make_shared<SKELETON::AnimSample>(t.skeleton, selected->animation);
	sample->setLooping(false);
	sample->seek(0);
	t.actionClip = { selected->id, sample, selected->metadata, std::max(.4f, sample->duration() / selected->rate) };
	if (!t.actionClip) {
		return false;
	}
	t.actionDuration = t.actionClip.duration;
	t.vault = plan.vault;
	m.heading = std::atan2(direction.x, direction.z) / radians;
	auto& clip = t.actionClip;
	clip.sample->seek(0);
	t.originStart = t.traversalStart - rotate(Motion::feet(*t.skeleton, *clip.sample->getPose(), t.rig), m.heading);
	clip.sample->seek(clip.sample->duration());
	t.originEnd = t.traversalEnd - rotate(Motion::feet(*t.skeleton, *clip.sample->getPose(), t.rig), m.heading);
	t.plantPhase = clip.metadata.Plant;
	t.releasePhase = clip.metadata.Release;
	if (clip.metadata.Contact && !t.vault) {
		bool reachable = false;
		// A lower t.ledge needs a later, tucked pose before both the hands
		// can be planted and the feet can stay above the ground. Determine
		// that reach from the actual t.skeleton instead of lifting the body.
		for (float phase = clip.metadata.Plant; phase < clip.metadata.Release - .06f; phase += 1.f / 120) {
			clip.sample->seek(phase * clip.sample->duration());
			const auto& pose = *clip.sample->getPose();
			const float lowest = Motion::feet(*t.skeleton, pose, t.rig).y;
			const float hands = Motion::hand(*t.skeleton, pose, clip.metadata.SingleHand, t.rig).y;
			t.plantPhase = phase;
			if (t.ledge.y + lowest - hands >= t.traversalStart.y + .025f) {
				reachable = true;
				break;
			}
		}
		if (!reachable) {
			return false;
		}
	}
	t.actionId = clip.id;
	t.progress = 0;
	t.traversing = true;
	m.motor.velocity = {};
	t.accepted = true;
	clip.sample->seek(0);

	t.visualFeet = t.traversalStart;
	return true;
}
void TraversalSystem::update(SCENE_SYSTEM::Scene& scene, CharacterMotorComponent& m, TraversalComponent& t, float dt) {
	auto& world = scene.physics3d();

	t.progress = std::min(1.f, t.progress + dt / t.actionDuration);
	const float phase = t.progress;
	const Vec3 raisedEnd { t.traversalEnd.x, t.traversalRaised.y, t.traversalEnd.z };
	Vec3 target;
	if (phase < .3f) {
		target = t.traversalStart + (t.traversalRaised - t.traversalStart) * (phase / .3f);
	}
	else if (phase < .8f) {
		target = t.traversalRaised + (raisedEnd - t.traversalRaised) * ((phase - .3f) / .5f);
	}
	else {
		target = raisedEnd + (t.traversalEnd - raisedEnd) * ((phase - .8f) / .2f);
	}
	if (pathClear(world, m.motor.capsule, target)) {
		m.motor.capsule.feet = target;
	}
	else {
		t.traversing = false;
		m.motor.grounded = false;
		t.cancelled = true;
	}
	auto& clip = t.actionClip;
	clip.sample->seek(phase * clip.sample->duration());
	const auto& pose = *clip.sample->getPose();
	Vec3 origin;
	if (clip.metadata.Contact) {
		origin = Motion::anchorOrigin(t.originStart, t.originEnd, t.ledge,
			rotate(Motion::hand(*t.skeleton, pose, clip.metadata.SingleHand, t.rig), m.heading), phase, t.plantPhase, t.releasePhase);
	}
	else {
		origin = MATH::Vector3f::Lerp(t.originStart, t.originEnd, Motion::smooth(0, 1, phase));
	}
	// Root motion remains in the pose. Warp its origin onto the ledge and
	// landing, rather than lifting a frozen pelvis along the capsule path.
	// Check the support feet against actual surfaces, including the far edge.
	const auto points = Motion::positions(*t.skeleton, pose);
	float lift = 0;
	Filter footFilter;
	// A braced foot can be below the ledge and against its vertical face.
	// Treating that foot as standing on top lifted the whole body away
	// from its planted hands. Require top support only when stepping out.
	if (!t.vault && phase < .8f) {
		footFilter.ignore = t.traversalObstacle;
	}
	for (const auto& name : t.rig.feet) {
		const int index = t.skeleton->findJointIndex(name);
		if (index < 0) {
			continue;
		}
		const auto p = origin + rotate(points[index] * t.rig.unitsToMeters, m.heading);
		const auto support = raycast(world, { p.x, t.traversalRaised.y + 1.f, p.z }, { 0, -4.5f, 0 }, footFilter);
		if (support && support.normal.y > .7f) {
			lift = std::max(lift, support.point.y + .025f - p.y);
		}
	}
	origin.y += lift;
	if (TESTHOOK::traversalContact) {
		TESTHOOK::traversalContact(scene, m, t, origin, pose, phase);
	}
	t.poseOffset = (rotate(origin - m.motor.capsule.feet, -m.heading));
	const int hips = t.skeleton->findJointIndex(t.rig.root);
	const auto hip = hips >= 0 ? points[hips] * t.rig.unitsToMeters : Vec3 { 0, t.rig.rootHeight, 0 };
	t.visualFeet = origin + rotate(hip, m.heading) - Vec3 { 0, t.rig.rootHeight, 0 };
	if (phase >= 1 && t.traversing) {
		t.traversing = false;
		m.motor.grounded = true;
		m.airTime = 0;
		t.completed = true;
	}
	m.jump = false;
}
}
#endif
