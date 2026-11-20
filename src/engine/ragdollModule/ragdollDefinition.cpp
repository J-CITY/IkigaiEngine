#include "ragdollDefinition.h"
#if USE_PHYSICS_3D
#include <map>
#include <set>
#include <stdexcept>

namespace IKIGAI::SKELETON {
namespace {
	void require(bool valid, const std::string& message) {
		if (!valid) {
			throw std::invalid_argument("Ragdoll: " + message);
		}
	}
	bool unit(MATH::QuaternionF q) {
		const float length = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		return std::isfinite(length) && std::abs(length - 1) < .001f;
	}
}
std::shared_ptr<const RagdollDefinition> RagdollDefinition::compile(const RagdollDescriptor& descriptor, std::shared_ptr<const Skeleton> skeleton) {
	require(descriptor.Version == 1, "unsupported Version");
	require(skeleton && skeleton->getNumJolts() > 0 && skeleton->getNumJolts() <= MAXBONES && skeleton->getNumJolts() == skeleton->joints().size(),
		"invalid skeleton");
	require(std::isfinite(descriptor.UnitsToMeters) && descriptor.UnitsToMeters > 0, "invalid UnitsToMeters");
	require(!descriptor.Segments.empty(), "Segments is empty");
	auto result = std::make_shared<RagdollDefinition>();
	// Keep the binding stable if the loader replaces its mutable skeleton resource.
	result->skeleton = std::make_shared<Skeleton>(*skeleton);
	result->unitsToMeters = descriptor.UnitsToMeters;
	result->boneSegments.assign(result->skeleton->joints().size(), -1);
	std::map<std::string, std::size_t> names {};
	for (std::size_t index = 0; index < result->skeleton->joints().size(); ++index) {
		const int parent = result->skeleton->joints()[index].parentIndex;
		require(parent >= -1 && parent < static_cast<int>(index), "skeleton must have parent-before-child order");
		const auto& bind = result->skeleton->joints()[index].bindPose;
		require(PHYSICS_COMMON::finite(bind.translation) && unit(bind.rotation) && PHYSICS_COMMON::finite(bind.scale)
				&& (bind.scale - MATH::Vector3f { 1, 1, 1 }).magnitude() < .001f,
			"unsupported bind TRS at " + result->skeleton->joints()[index].name);
	}
	for (const auto& segment : descriptor.Segments) {
		const auto index = result->segments.size();
		require(!segment.Name.empty() && names.emplace(segment.Name, index).second, "duplicate/empty segment " + segment.Name);
		const int bone = result->skeleton->findJointIndex(segment.Bone);
		require(bone >= 0, "missing bone " + segment.Bone);
		require(result->boneSegments[bone] < 0, "duplicate bone " + segment.Bone);
		require(PHYSICS_COMMON::finite(segment.Offset) && unit(segment.Rotation), "invalid offset/rotation: " + segment.Name);
		require(std::isfinite(segment.Mass) && segment.Mass > 0 && std::isfinite(segment.Friction) && segment.Friction >= 0,
			"invalid mass/material: " + segment.Name);
		RagdollSegment resolved {};
		resolved.bone = static_cast<std::uint32_t>(bone);
		resolved.offset = segment.Offset;
		resolved.rotation = segment.Rotation;
		if (segment.Shape == "Box") {
			resolved.body.shape = PHYSICS::Shape::box(segment.HalfSize);
		}
		else if (segment.Shape == "Sphere") {
			resolved.body.shape = PHYSICS::Shape::sphere(segment.Radius);
		}
		else {
			throw std::invalid_argument("Ragdoll: unsupported Shape: " + segment.Shape);
		}
		resolved.body.mass = segment.Mass;
		resolved.body.friction = segment.Friction;
		resolved.body.restitution = 0;
		resolved.body.allowSleep = false;
		resolved.body.layer = descriptor.Layer;
		resolved.body.mask = descriptor.Mask;
		result->boneSegments[bone] = static_cast<int>(index);
		result->segments.push_back(resolved);
	}
	require(names.contains(descriptor.Pelvis), "Pelvis must name a segment");
	result->pelvis = names.at(descriptor.Pelvis);
	std::vector<int> parents(result->segments.size(), -1);
	for (const auto& source : descriptor.Joints) {
		require(names.contains(source.Parent) && names.contains(source.Child), "unknown joint segment");
		RagdollJoint resolved {};
		resolved.parent = names.at(source.Parent);
		resolved.child = names.at(source.Child);
		require(resolved.parent != resolved.child && resolved.child != result->pelvis && parents[resolved.child] < 0,
			"invalid parent for " + source.Child);
		parents[resolved.child] = static_cast<int>(resolved.parent);
		auto& joint = resolved.joint;
		joint.bodyA = resolved.parent + 1;
		joint.bodyB = resolved.child + 1;
		joint.anchorA = source.AnchorA;
		joint.anchorB = source.AnchorB;
		joint.frameA = source.FrameA;
		joint.frameB = source.FrameB;
		if (source.Kind == "Hinge") {
			joint.kind = PHYSICS::RigidJointKind::Hinge;
		}
		else if (source.Kind == "ConeTwist") {
			joint.kind = PHYSICS::RigidJointKind::ConeTwist;
		}
		else {
			throw std::invalid_argument("Ragdoll: unsupported joint Kind: " + source.Kind);
		}
		joint.swingLimit = source.SwingLimit;
		joint.twistMin = source.TwistMin;
		joint.twistMax = source.TwistMax;
		joint.damping = source.Damping;
		joint.collideConnected = source.CollideConnected;
		joint.validate();
		int ancestor = result->skeleton->joints()[result->segments[resolved.child].bone].parentIndex;
		const int parentBone = static_cast<int>(result->segments[resolved.parent].bone);
		while (ancestor >= 0 && ancestor != parentBone) {
			ancestor = result->skeleton->joints()[ancestor].parentIndex;
		}
		require(ancestor == parentBone, "joint parent must be a skeletal ancestor: " + source.Child);
		result->joints.push_back(resolved);
	}
	for (std::size_t index = 0; index < parents.size(); ++index) {
		std::set<std::size_t> visited {};
		auto node = index;
		while (node != result->pelvis) {
			require(visited.insert(node).second && parents[node] >= 0, "disconnected or cyclic segment graph");
			node = static_cast<std::size_t>(parents[node]);
		}
	}
	return result;
}
}
#endif
