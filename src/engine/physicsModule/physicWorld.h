#pragma once
// Standalone kernel: ECS and rendering consume handles and snapshots only.
#include "body.h"
#include "collisionPipeline.h"
#include "contacts.h"
#include "impactEvent.h"
#include "narrowPhase.h"
#include "physicsCommon/constraints.h"
#include "rigidJoint.h"
#include <array>
#include <map>
#include <span>
#include <vector>

namespace IKIGAI::PHYSICS {
using Id = PHYSICS_COMMON::Id;
using Vec3 = MATH::Vector3f;
using ForceField = PHYSICS_COMMON::ForceField<Vec3>;
using ForceFieldRegistry = PHYSICS_COMMON::ForceRegistry<Vec3>;
using DistanceConstraint = PHYSICS_COMMON::DistanceConstraint;
enum class BodyType { Static, Dynamic };
using Shape = COLLISION::Shape3D;
struct BodyDef {
	Shape shape;
	BodyType type = BodyType::Dynamic;
	Vec3 position, velocity, angularVelocity, acceleration;
	MATH::QuaternionF orientation { 0, 0, 0, 1 };
	float mass = 1, friction = 0.5f, restitution = 0.1f, gravityScale = 1;
	float linearDamping = 0.05f, angularDamping = 0.05f;
	bool allowSleep = true;
	std::uint32_t layer = 1, mask = ~0u;
};
struct Body : RigidBody {
	Id id = 0;
	Shape shape;
	BodyType type = BodyType::Dynamic;
	Vec3 force, torque, extraAcceleration;
	float friction = 0.5f, restitution = 0.1f, gravityScale = 1;
	std::uint32_t layer = 1, mask = ~0u;
};
inline COLLISION::ShapeView3D geometry(const Body& b) {
	return { b.shape, b.getPosition(),
		{ b.getDirectionInWorldSpace({ 1, 0, 0 }), b.getDirectionInWorldSpace({ 0, 1, 0 }), b.getDirectionInWorldSpace({ 0, 0, 1 }) } };
}
struct Particle {
	Vec3 position, velocity;
	float inverseMass = 1;
};
using Triangle = std::array<std::size_t, 3>;
using Tetrahedron = std::array<std::size_t, 4>;
struct SoftBodyDef {
	std::vector<Particle> particles;
	std::vector<DistanceConstraint> links;
	std::vector<Triangle> triangles;
	std::vector<Tetrahedron> tetrahedra;
	float radius = 0.05f, compliance = 0, volumeCompliance = 0, damping = 0.5f, friction = 0.3f;
	bool selfCollision = false, cloth = false, impermeable = true;
	std::uint32_t layer = 1, mask = ~0u;
};
struct SoftBody : SoftBodyDef {
	Id id = 0;
	std::vector<float> restVolumes, linkLambda, volumeLambda;
	std::vector<Vec3> previous;
};
struct ClothDef {
	int width = 16, height = 16;
	float spacing = 0.1f, particleMass = 0.01f, radius = 0.02f;
	float stretchCompliance = 0, shearCompliance = 1e-5f, bendCompliance = 0.001f;
	float damping = 0.5f, friction = 0.3f;
	Vec3 position, axisU { 1, 0, 0 }, axisV { 0, -1, 0 };
	bool pinTop = true, selfCollision = true, impermeable = true;
};
struct FluidDef {
	std::vector<Particle> particles;
	float radius = 0.04f, smoothingRadius = 0.16f, particleMass = 0.512f;
	float restDensity = 1000, viscosity = 0.05f, relaxation = 0.001f;
	std::uint32_t layer = 1, mask = ~0u;
};
struct Fluid : FluidDef {
	Id id = 0;
	std::vector<Vec3> previous;
};
struct AttachmentDef {
	Id soft = 0, body = 0;
	std::size_t particle = 0;
	Vec3 point; // local on body; body == 0 means a world anchor
	float compliance = 0;
};
struct Attachment : AttachmentDef {
	Id id = 0;
	float lambda = 0;
};
struct Settings {
	Vec3 gravity { 0, -9.81f, 0 };
	float cellSize = 2;
	int substeps = 2, velocityIterations = 12, positionIterations = 12, particleIterations = 6;
	unsigned maximumContacts = 1024;
};
struct DebugDraw {
	std::function<void(Vec3, Vec3, std::uint32_t)> line;
	std::function<void(Vec3, float, std::uint32_t)> sphere;
	std::function<void(Vec3, Vec3, Vec3, std::uint32_t)> triangle;
};
class PhysicWorld {
public:
	explicit PhysicWorld(unsigned maxContacts = 1024, unsigned iterations = 0);
	explicit PhysicWorld(Settings settings, CollisionPipeline pipeline = {}, unsigned maxContacts = 1024);
	explicit PhysicWorld(CollisionPipeline pipeline)
		: PhysicWorld(Settings {}, std::move(pipeline)) { }
	const CollisionPipeline& collisionPipeline() const {
		return mPipeline;
	}
	PhysicWorld(const PhysicWorld&) = delete;
	PhysicWorld& operator=(const PhysicWorld&) = delete;
	Settings settings;
	ForceFieldRegistry forces;
	Id createBody(const BodyDef& def);
	// Creates candidates between simulation steps. Failure publishes none and does not wake contact islands.
	std::vector<Id> createBodies(std::span<const BodyDef> definitions);
	std::vector<Id> queryBodies(const MATH::GEOMETRY::Aabb<3>& bounds);
	Id createJoint(const RigidJointDef& def);
	void destroyJoint(Id id);
	const std::map<Id, RigidJoint>& joints() const;
	bool collisionEnabled(Id bodyA, Id bodyB) const;
	Id createSoftBody(const SoftBodyDef& def);
	Id createCloth(const ClothDef& def);
	Id createFluid(const FluidDef& def);
	Id createAttachment(const AttachmentDef& def);
	void destroyBody(Id id);
	void destroySoftBody(Id id);
	void destroyCloth(Id id) {
		destroySoftBody(id);
	}
	void destroyFluid(Id id);
	void destroyAttachment(Id id);
	void clear();
	Body* body(Id id);
	SoftBody* softBody(Id id);
	SoftBody* cloth(Id id) {
		auto* s = softBody(id);
		return s && s->cloth ? s : nullptr;
	}
	Fluid* fluid(Id id);
	const auto& bodies() const {
		return mBodies;
	}
	const auto& softBodies() const {
		return mSoft;
	}
	const auto& fluids() const {
		return mFluids;
	}
	const auto& attachments() const {
		return mAttachments;
	}
	std::span<const Contact> contacts() const {
		return { mContacts.data(), mContactCount };
	}
	std::span<const ImpactEvent> impacts() const {
		return mImpacts;
	}
	std::uint64_t tick() const {
		return mTick;
	}
	double simulationTime() const {
		return mTime;
	}
	void applyForce(Id id, Vec3 force, Vec3 worldPoint);
	void applyImpulse(Id id, Vec3 impulse, Vec3 worldPoint);
	void teleport(Id id, Vec3 position, MATH::QuaternionF orientation);
	void setVelocity(Id id, Vec3 velocity, Vec3 angular = {});
	void wake(Id id);
	void step(float dt);
	void debugDraw(const DebugDraw&) const;
	void startFrame() { } // compatibility: step owns accumulator lifetime
	void runPhysics(float dt) {
		step(dt);
	}

private:
	CollisionPipeline mPipeline;
	COLLISION::Manifold3D detect(const COLLISION::ShapeView3D& a, const COLLISION::ShapeView3D& b) const;
	Id mNextId = 1;
	double mTime = 0;
	std::map<Id, Body> mBodies;
	std::map<Id, RigidJoint> mJoints {};
	std::map<std::pair<Id, Id>, std::size_t> mJointCollisionExclusions {};
	std::map<Id, SoftBody> mSoft;
	std::map<Id, Fluid> mFluids;
	std::map<Id, Attachment> mAttachments;
	std::vector<Contact> mContacts;
	std::size_t mContactCount = 0;
	ContactResolver mResolver;
	std::vector<ImpactEvent> mImpacts {};
	std::map<std::pair<Id, Id>, size_t> mImpactLookup {};
	std::uint64_t mTick = 0;
	void solveRigid(float dt, bool jointIteration = false);
	void stepParticles(float dt);
	void projectParticle(
		Particle&, float radius, float friction, std::uint32_t layer, std::uint32_t mask, float dt, UTILS::SPATIAL::ISpatialIndex<3>& solids);
	void solveAttachments(float dt);
};
using World = PhysicWorld;
}
