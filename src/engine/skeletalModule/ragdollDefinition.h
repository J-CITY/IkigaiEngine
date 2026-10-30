#pragma once
#if USE_PHYSICS_3D
#include "physicsModule/physicWorld.h"
#include "skeleton.h"
#include <serdepp/adaptor/nlohmann_json.hpp>

namespace IKIGAI::SKELETON {
struct RagdollRotation {
	float X = 0;
	float Y = 0;
	float Z = 0;
	float W = 1;
	RagdollRotation() = default;
	RagdollRotation(MATH::QuaternionF q)
		: X(q.x)
		, Y(q.y)
		, Z(q.z)
		, W(q.w) { }
	operator MATH::QuaternionF() const {
		return { X, Y, Z, W };
	}
	template <class C> static auto serde(C& c, RagdollRotation& v) {
		serde::serde_struct(c, v)
			.field(&RagdollRotation::X, "X")
			.field(&RagdollRotation::Y, "Y")
			.field(&RagdollRotation::Z, "Z")
			.field(&RagdollRotation::W, "W");
	}
};
struct RagdollSegmentDescriptor {
	std::string Name {};
	std::string Bone {};
	std::string Shape = "Box";
	MATH::Vector3f HalfSize { .1f, .2f, .1f };
	float Radius = .1f;
	MATH::Vector3f Offset {};
	RagdollRotation Rotation {};
	float Mass = 1;
	float Friction = .5f;
	template <class C> static auto serde(C& c, RagdollSegmentDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&RagdollSegmentDescriptor::Name, "Name")
			.field(&RagdollSegmentDescriptor::Bone, "Bone")
			.field(&RagdollSegmentDescriptor::Shape, "Shape", default_ { std::string("Box") })
			.field(&RagdollSegmentDescriptor::HalfSize, "HalfSize", default_ { MATH::Vector3f { .1f, .2f, .1f } })
			.field(&RagdollSegmentDescriptor::Radius, "Radius", default_ { .1f })
			.field(&RagdollSegmentDescriptor::Offset, "Offset", default_ { MATH::Vector3f {} })
			.field(&RagdollSegmentDescriptor::Rotation, "Rotation", default_ { RagdollRotation {} })
			.field(&RagdollSegmentDescriptor::Mass, "Mass", default_ { 1.f })
			.field(&RagdollSegmentDescriptor::Friction, "Friction", default_ { .5f });
	}
};
struct RagdollJointDescriptor {
	std::string Parent {};
	std::string Child {};
	std::string Kind = "ConeTwist";
	MATH::Vector3f AnchorA {};
	MATH::Vector3f AnchorB {};
	RagdollRotation FrameA {};
	RagdollRotation FrameB {};
	float SwingLimit = .78539816f;
	float TwistMin = -.78539816f;
	float TwistMax = .78539816f;
	float Damping = .05f;
	bool CollideConnected = false;
	template <class C> static auto serde(C& c, RagdollJointDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&RagdollJointDescriptor::Parent, "Parent")
			.field(&RagdollJointDescriptor::Child, "Child")
			.field(&RagdollJointDescriptor::Kind, "Kind", default_ { std::string("ConeTwist") })
			.field(&RagdollJointDescriptor::AnchorA, "AnchorA")
			.field(&RagdollJointDescriptor::AnchorB, "AnchorB")
			.field(&RagdollJointDescriptor::FrameA, "FrameA", default_ { RagdollRotation {} })
			.field(&RagdollJointDescriptor::FrameB, "FrameB", default_ { RagdollRotation {} })
			.field(&RagdollJointDescriptor::SwingLimit, "SwingLimit", default_ { .78539816f })
			.field(&RagdollJointDescriptor::TwistMin, "TwistMin", default_ { -.78539816f })
			.field(&RagdollJointDescriptor::TwistMax, "TwistMax", default_ { .78539816f })
			.field(&RagdollJointDescriptor::Damping, "Damping", default_ { .05f })
			.field(&RagdollJointDescriptor::CollideConnected, "CollideConnected", default_ { false });
	}
};
struct RagdollDescriptor {
	int Version = 1;
	std::string Skeleton {};
	std::string Pelvis {};
	float UnitsToMeters = 1;
	std::uint32_t Layer = 1;
	std::uint32_t Mask = ~0u;
	std::vector<RagdollSegmentDescriptor> Segments {};
	std::vector<RagdollJointDescriptor> Joints {};
	template <class C> static auto serde(C& c, RagdollDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&RagdollDescriptor::Version, "Version")
			.field(&RagdollDescriptor::Skeleton, "Skeleton")
			.field(&RagdollDescriptor::Pelvis, "Pelvis")
			.field(&RagdollDescriptor::UnitsToMeters, "UnitsToMeters", default_ { 1.f })
			.field(&RagdollDescriptor::Layer, "Layer", default_ { std::uint32_t(1) })
			.field(&RagdollDescriptor::Mask, "Mask", default_ { ~std::uint32_t(0) })
			.field(&RagdollDescriptor::Segments, "Segments")
			.field(&RagdollDescriptor::Joints, "Joints");
	}
};
struct RagdollSegment {
	std::uint32_t bone = 0;
	PHYSICS::BodyDef body {};
	MATH::Vector3f offset {};
	MATH::QuaternionF rotation {};
};
struct RagdollJoint {
	std::size_t parent = 0;
	std::size_t child = 0;
	PHYSICS::RigidJointDef joint {};
};
struct RagdollDefinition {
	std::shared_ptr<const Skeleton> skeleton {};
	std::vector<RagdollSegment> segments {};
	std::vector<RagdollJoint> joints {};
	std::vector<int> boneSegments {};
	std::size_t pelvis = 0;
	float unitsToMeters = 1;
	static std::shared_ptr<const RagdollDefinition> compile(const RagdollDescriptor&, std::shared_ptr<const Skeleton>);
};
}
#endif
