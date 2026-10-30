#include "ragdollInstance.h"
#if USE_PHYSICS_3D
#include <algorithm>
#include <stdexcept>

namespace IKIGAI::SKELETON {
namespace {
	using V = MATH::Vector3f;
	using Q = MATH::QuaternionF;
	bool unit(Q value) {
		const float length = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
		return std::isfinite(length) && std::abs(length - 1) < .001f;
	}
	V angularVelocity(Q before, Q after, float dt) {
		auto delta = Q::Normalize(after * Q::Conjugate(before));
		if (delta.w < 0) {
			delta = delta * -1.f;
		}
		const V vector { delta.x, delta.y, delta.z };
		const float sine = vector.magnitude();
		return sine > 1e-7f ? vector * (2 * std::atan2(sine, delta.w) / (sine * dt)) : V {};
	}
}
RagdollInstance::RagdollInstance(std::shared_ptr<const RagdollDefinition> definition)
	: mDefinition(std::move(definition)) {
	if (!mDefinition || !mDefinition->skeleton) {
		throw std::invalid_argument("Ragdoll requires a compiled definition");
	}
}
RagdollInstance::~RagdollInstance() {
	reset();
}
std::vector<RagdollFrame> RagdollInstance::modelFrames(const Skeleton& skeleton, const Pose& pose, float units) {
	if (pose.numKeyframes != skeleton.joints().size() || pose.numKeyframes > MAXBONES || !std::isfinite(units) || units <= 0) {
		throw std::invalid_argument("Ragdoll pose/skeleton mismatch");
	}
	std::vector<RagdollFrame> result(pose.numKeyframes);
	for (std::size_t index = 0; index < result.size(); ++index) {
		const auto& key = pose.keyframes[index];
		if (!PHYSICS_COMMON::finite(key.translation) || !unit(key.rotation) || !PHYSICS_COMMON::finite(key.scale)
			|| (key.scale - V { 1, 1, 1 }).magnitude() > .001f) {
			throw std::invalid_argument("Ragdoll requires finite rigid bone TRS with unit bone scale");
		}
		const int parent = skeleton.joints()[index].parentIndex;
		if (parent < -1 || parent >= static_cast<int>(index)) {
			throw std::invalid_argument("Invalid ragdoll skeleton hierarchy");
		}
		const RagdollFrame base = parent >= 0 ? result[parent] : RagdollFrame {};
		result[index] = { base.position + base.rotation * (key.translation * units), Q::Normalize(base.rotation * key.rotation) };
	}
	return result;
}
void RagdollInstance::activate(
	std::shared_ptr<PHYSICS::World> world, const Pose& pose, RagdollFrame root, const Pose* previous, const RagdollFrame* previousRoot, float dt) {
	if (simulated()) {
		return;
	}
	if (!world || !PHYSICS_COMMON::finite(root.position) || !unit(root.rotation)) {
		throw std::invalid_argument("Invalid ragdoll world/root");
	}
	const auto frames = modelFrames(*mDefinition->skeleton, pose, mDefinition->unitsToMeters);
	std::vector<RagdollFrame> old {};
	if (previous && previousRoot && std::isfinite(dt) && dt > 0) {
		if (!PHYSICS_COMMON::finite(previousRoot->position) || !unit(previousRoot->rotation)) {
			throw std::invalid_argument("Invalid previous root");
		}
		old = modelFrames(*mDefinition->skeleton, *previous, mDefinition->unitsToMeters);
	}
	std::vector<PHYSICS::Id> bodies {};
	std::vector<PHYSICS::Id> joints {};
	bodies.reserve(mDefinition->segments.size());
	joints.reserve(mDefinition->joints.size());
	try {
		for (const auto& segment : mDefinition->segments) {
			auto body = segment.body;
			const auto& bone = frames[segment.bone];
			body.position = root.position + root.rotation * (bone.position + bone.rotation * segment.offset);
			body.orientation = Q::Normalize(root.rotation * bone.rotation * segment.rotation);
			if (!old.empty()) {
				const auto& before = old[segment.bone];
				const auto oldPosition = previousRoot->position + previousRoot->rotation * (before.position + before.rotation * segment.offset);
				body.velocity = (body.position - oldPosition) * (1.f / dt);
				body.angularVelocity = angularVelocity(previousRoot->rotation * before.rotation * segment.rotation, body.orientation, dt);
			}
			bodies.push_back(world->createBody(body));
		}
		for (const auto& source : mDefinition->joints) {
			auto joint = source.joint;
			joint.bodyA = bodies[source.parent];
			joint.bodyB = bodies[source.child];
			joints.push_back(world->createJoint(joint));
		}
	} catch (...) {
		for (auto id : joints) {
			world->destroyJoint(id);
		}
		for (auto id : bodies) {
			world->destroyBody(id);
		}
		throw;
	}
	mWorld = world;
	mBodies = std::move(bodies);
	mJoints = std::move(joints);
	mReference = pose;
	mOutput = pose;
	mRoot = mSimulationRoot = root;
	mPelvisOffset = world->body(mBodies[mDefinition->pelvis])->getPosition() - root.position;
	mCurrent.resize(mBodies.size());
	for (std::size_t index = 0; index < mBodies.size(); ++index) {
		const auto& body = *world->body(mBodies[index]);
		mCurrent[index] = { body.getPosition(), body.getOrientation() };
	}
	mPrevious = mCurrent;
	interpolate(1);
}
void RagdollInstance::reset() {
	if (auto world = mWorld.lock()) {
		for (auto id : mJoints) {
			world->destroyJoint(id);
		}
		for (auto id : mBodies) {
			world->destroyBody(id);
		}
	}
	mJoints.clear();
	mBodies.clear();
	mWorld.reset();
}
bool RagdollInstance::simulated() const {
	return !mBodies.empty() && !mWorld.expired();
}
void RagdollInstance::capture() {
	if (auto world = mWorld.lock(); world && !mBodies.empty()) {
		for (auto id : mBodies) {
			if (!world->body(id)) {
				reset();
				return;
			}
		}
		mPrevious = mCurrent;
		for (std::size_t index = 0; index < mBodies.size(); ++index) {
			const auto& body = *world->body(mBodies[index]);
			mCurrent[index] = { body.getPosition(), body.getOrientation() };
		}
		mSimulationRoot.position = mCurrent[mDefinition->pelvis].position - mPelvisOffset;
	}
}
void RagdollInstance::interpolate(float alpha) {
	if (!simulated()) {
		return;
	}
	alpha = std::clamp(alpha, 0.f, 1.f);
	std::vector<RagdollFrame> frames(mCurrent.size());
	for (std::size_t index = 0; index < frames.size(); ++index) {
		frames[index] = { V::Lerp(mPrevious[index].position, mCurrent[index].position, alpha),
			Q::Normalize(Q::Slerp(mPrevious[index].rotation, mCurrent[index].rotation, alpha)) };
	}
	mRoot.position = frames[mDefinition->pelvis].position - mPelvisOffset;
	const Q inverseRoot = Q::Conjugate(mRoot.rotation);
	std::vector<RagdollFrame> bones(mDefinition->skeleton->joints().size());
	mOutput = mReference;
	mBoundsRadius = 0;
	for (std::size_t index = 0; index < bones.size(); ++index) {
		const int parent = mDefinition->skeleton->joints()[index].parentIndex;
		const RagdollFrame base = parent >= 0 ? bones[parent] : RagdollFrame {};
		const int segmentIndex = mDefinition->boneSegments[index];
		if (segmentIndex < 0) {
			bones[index] = { base.position + base.rotation * (mReference.keyframes[index].translation * mDefinition->unitsToMeters),
				Q::Normalize(base.rotation * mReference.keyframes[index].rotation) };
		}
		else {
			const auto& segment = mDefinition->segments[segmentIndex];
			const auto rotation = Q::Normalize(frames[segmentIndex].rotation * Q::Conjugate(segment.rotation));
			const auto position = frames[segmentIndex].position - rotation * segment.offset;
			bones[index] = { inverseRoot * (position - mRoot.position), Q::Normalize(inverseRoot * rotation) };
			mOutput.keyframes[index].translation
				= Q::Conjugate(base.rotation) * (bones[index].position - base.position) * (1.f / mDefinition->unitsToMeters);
			mOutput.keyframes[index].rotation = Q::Normalize(Q::Conjugate(base.rotation) * bones[index].rotation);
		}
		mBoundsRadius = std::max(mBoundsRadius, bones[index].position.magnitude());
	}
	// Conservative padding for geometry extending beyond joint positions.
	for (const auto& segment : mDefinition->segments) {
		const float radius
			= segment.body.shape.kind == PHYSICS::Shape::Kind::Sphere ? segment.body.shape.radius : segment.body.shape.halfSize.magnitude();
		mBoundsRadius = std::max(mBoundsRadius, bones[segment.bone].position.magnitude() + segment.offset.magnitude() + radius);
	}
}
void RagdollInstance::applyImpulse(std::size_t segment, V impulse, V point) {
	if (segment >= mBodies.size()) {
		throw std::out_of_range("Ragdoll segment ID");
	}
	if (auto world = mWorld.lock()) {
		world->applyImpulse(mBodies[segment], impulse, point);
	}
}
void RagdollInstance::update(float) { }
Pose* RagdollInstance::getPose() {
	return &mOutput;
}
std::unique_ptr<IAnimationPlayable> RagdollInstance::clone() const {
	auto result = std::make_unique<RagdollInstance>(mDefinition);
	result->mReference = mReference;
	result->mOutput = mOutput;
	return result;
}
RagdollFrame RagdollInstance::root() const {
	return mRoot;
}
RagdollFrame RagdollInstance::simulationRoot() const {
	return mSimulationRoot;
}
float RagdollInstance::boundsRadius() const {
	return mBoundsRadius;
}
const std::vector<PHYSICS::Id>& RagdollInstance::bodies() const {
	return mBodies;
}
const RagdollDefinition& RagdollInstance::definition() const {
	return *mDefinition;
}
}
#endif
