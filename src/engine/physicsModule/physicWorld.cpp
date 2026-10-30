#include "physicWorld.h"
#include "rigidJointSolver.h"
#include "spatialGrid.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace IKIGAI::PHYSICS {
namespace {
	void require(bool ok, const char* message) {
		if (!ok) {
			throw std::invalid_argument(message);
		}
	}
	bool finite(Vec3 v) {
		return PHYSICS_COMMON::finite(v);
	}
	bool finite(MATH::QuaternionF q) {
		return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w)
			&& q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w > 1e-12f;
	}
	unsigned capacity(unsigned count) {
		require(count > 0 && count <= 1048576, "Invalid contact capacity");
		return count;
	}
}
PhysicWorld::PhysicWorld(unsigned count, unsigned iterations)
	: mContacts(capacity(count))
	, mResolver(iterations ? iterations : 12) {
	settings.maximumContacts = count;
	if (iterations) {
		settings.velocityIterations = settings.positionIterations = static_cast<int>(iterations);
	}
}
PhysicWorld::PhysicWorld(Settings config, CollisionPipeline pipeline, unsigned count)
	: settings(config)
	, mPipeline(std::move(pipeline))
	, mContacts(capacity(count))
	, mResolver(12) {
	// The legacy explicit maxContacts argument remains a hard cap.
	if (count != 1024) {
		settings.maximumContacts = count;
	}
	mPipeline.validate();
}
COLLISION::Manifold3D PhysicWorld::detect(const COLLISION::ShapeView3D& a, const COLLISION::ShapeView3D& b) const {
	COLLISION::Manifold3D m;
	auto status = mPipeline.narrowPhase->collide(a, b, m);
	if (status == COLLISION::Status::Unsupported) {
		throw std::runtime_error("Narrow phase does not support shape pair");
	}
	if (status == COLLISION::Status::Separated) {
		return {};
	}
	if (m.count < 1 || m.count > 4 || !finite(m.normal) || std::abs(m.normal.squareMagnitude() - 1) > 0.001f) {
		throw std::runtime_error("Invalid 3D collision manifold");
	}
	for (int i = 0; i < m.count; ++i) {
		if (!finite(m.points[i].position) || !std::isfinite(m.points[i].penetration) || m.points[i].penetration < 0) {
			throw std::runtime_error("Invalid 3D contact point");
		}
	}
	return m;
}
Body* PhysicWorld::body(Id id) {
	auto i = mBodies.find(id);
	return i == mBodies.end() ? nullptr : &i->second;
}
SoftBody* PhysicWorld::softBody(Id id) {
	auto i = mSoft.find(id);
	return i == mSoft.end() ? nullptr : &i->second;
}
Fluid* PhysicWorld::fluid(Id id) {
	auto i = mFluids.find(id);
	return i == mFluids.end() ? nullptr : &i->second;
}
Id PhysicWorld::createBody(const BodyDef& d) {
	require(finite(d.position) && finite(d.velocity) && finite(d.angularVelocity) && finite(d.acceleration) && finite(d.orientation),
		"Nonfinite body state");
	require(d.type == BodyType::Static || d.type == BodyType::Dynamic, "Invalid body type");
	require(std::isfinite(d.mass) && d.mass > 0 && std::isfinite(d.friction) && d.friction >= 0 && std::isfinite(d.restitution) && d.restitution >= 0
			&& d.restitution <= 1 && std::isfinite(d.gravityScale) && std::isfinite(d.linearDamping) && d.linearDamping >= 0
			&& std::isfinite(d.angularDamping) && d.angularDamping >= 0,
		"Invalid body material");
	require(d.shape.kind == Shape::Kind::Sphere || d.shape.kind == Shape::Kind::Box || d.shape.kind == Shape::Kind::Convex, "Invalid shape kind");
	Body b;
	b.shape = d.shape.kind == Shape::Kind::Sphere ? Shape::sphere(d.shape.radius)
		: d.shape.kind == Shape::Kind::Box		  ? Shape::box(d.shape.halfSize)
												  : Shape::convex(d.shape.polyhedron);
	if (d.shape.kind == Shape::Kind::Convex) {
		require(d.shape.polyhedron->center.magnitude() < 1e-3f, "Convex body vertices must be centered at COM");
	}
	b.id = mNextId++;
	b.type = d.type;
	b.friction = d.friction;
	b.restitution = d.restitution;
	b.gravityScale = d.gravityScale;
	b.layer = d.layer;
	b.mask = d.mask;
	b.extraAcceleration = d.acceleration;
	b.setPosition(d.position);
	b.setOrientation(d.orientation);
	bool dynamic = d.type == BodyType::Dynamic;
	b.setInverseMass(dynamic ? 1 / d.mass : 0);
	b.setVelocity(dynamic ? d.velocity : Vec3 {});
	b.setRotation(dynamic ? d.angularVelocity : Vec3 {});
	b.setDamping(std::exp(-d.linearDamping), std::exp(-d.angularDamping));
	b.setCanSleep(d.allowSleep);
	b.setAwake(true);
	if (dynamic) {
		Vec3 inertia;
		if (d.shape.kind == Shape::Kind::Sphere) {
			inertia = Vec3(0.4f * d.mass * d.shape.radius * d.shape.radius);
		}
		else {
			auto h = d.shape.halfSize;
			inertia = Vec3(h.y * h.y + h.z * h.z, h.x * h.x + h.z * h.z, h.x * h.x + h.y * h.y) * (d.mass / 3);
		}
		b.setInertiaTensor(d.shape.kind == Shape::Kind::Convex ? d.shape.polyhedron->inertia * d.mass
															   : MATH::Matrix3f(inertia.x, 0, 0, 0, inertia.y, 0, 0, 0, inertia.z));
	}
	b.calculateDerivedData();
	mBodies.emplace(b.id, b);
	return b.id;
}
Id PhysicWorld::createJoint(const RigidJointDef& definition) {
	definition.validate();
	require(body(definition.bodyA) && body(definition.bodyB), "Rigid joint requires existing bodies");
	require(body(definition.bodyA)->type == BodyType::Dynamic || body(definition.bodyB)->type == BodyType::Dynamic,
		"Rigid joint requires at least one dynamic body");
	RigidJoint joint {};
	static_cast<RigidJointDef&>(joint) = definition;
	joint.id = mNextId++;
	mJoints.emplace(joint.id, joint);
	if (!joint.collideConnected) {
		++mJointCollisionExclusions[std::minmax(joint.bodyA, joint.bodyB)];
	}
	wake(definition.bodyA);
	wake(definition.bodyB);
	return joint.id;
}
void PhysicWorld::destroyJoint(Id id) {
	if (auto found = mJoints.find(id); found != mJoints.end()) {
		if (!found->second.collideConnected) {
			const auto pair = std::minmax(found->second.bodyA, found->second.bodyB);
			if (auto exclusion = mJointCollisionExclusions.find(pair); exclusion != mJointCollisionExclusions.end()) {
				if (--exclusion->second == 0) {
					mJointCollisionExclusions.erase(exclusion);
				}
			}
		}
		wake(found->second.bodyA);
		wake(found->second.bodyB);
		mJoints.erase(found);
	}
}
const std::map<Id, RigidJoint>& PhysicWorld::joints() const {
	return mJoints;
}
bool PhysicWorld::collisionEnabled(Id a, Id b) const {
	return !mJointCollisionExclusions.contains(std::minmax(a, b));
}
void PhysicWorld::destroyBody(Id id) {
	mPipeline.broadPhase->remove(id);
	// Removing a support may invalidate any resting contact island.
	for (auto& [bid, b] : mBodies) {
		if (b.type == BodyType::Dynamic) {
			b.setAwake(true);
		}
	}
	std::erase_if(mAttachments, [=](auto& e) { return e.second.body == id; });
	std::vector<Id> jointsToRemove {};
	for (const auto& [jointId, joint] : mJoints) {
		if (joint.bodyA == id || joint.bodyB == id) {
			jointsToRemove.push_back(jointId);
		}
	}
	for (auto jointId : jointsToRemove) {
		destroyJoint(jointId);
	}
	forces.removeTarget(id);
	mContactCount = 0;
	mBodies.erase(id);
}
void PhysicWorld::destroySoftBody(Id id) {
	std::erase_if(mAttachments, [=](auto& e) { return e.second.soft == id; });
	forces.removeTarget(id);
	mSoft.erase(id);
}
void PhysicWorld::destroyFluid(Id id) {
	forces.removeTarget(id);
	mFluids.erase(id);
}
void PhysicWorld::destroyAttachment(Id id) {
	mAttachments.erase(id);
}
void PhysicWorld::clear() {
	mImpacts.clear();
	mImpactLookup.clear();
	mPipeline.broadPhase->reset(settings.cellSize);
	mContactCount = 0;
	mAttachments.clear();
	mJoints.clear();
	mJointCollisionExclusions.clear();
	mBodies.clear();
	mSoft.clear();
	mFluids.clear();
	forces.clear();
	mTime = 0;
}
void PhysicWorld::wake(Id id) {
	std::set<Id> seen;
	std::vector<Id> pending { id };
	while (!pending.empty()) {
		auto current = pending.back();
		pending.pop_back();
		if (!seen.insert(current).second) {
			continue;
		}
		auto* b = body(current);
		if (!b) {
			continue;
		}
		if (b->type == BodyType::Static && current != id) {
			continue;
		}
		if (b->type == BodyType::Dynamic) {
			b->setAwake(true);
		}
		for (const auto& [jointId, joint] : mJoints) {
			if (joint.bodyA == current) {
				pending.push_back(joint.bodyB);
			}
			if (joint.bodyB == current) {
				pending.push_back(joint.bodyA);
			}
		}
		for (auto c : contacts()) {
			for (int i = 0; i < 2; ++i) {
				if (c.body[i] == b && c.body[1 - i]) {
					for (auto& [other, member] : mBodies) {
						if (&member == c.body[1 - i]) {
							pending.push_back(other);
						}
					}
				}
			}
		}
	}
}
void PhysicWorld::applyForce(Id id, Vec3 force, Vec3 point) {
	require(finite(force) && finite(point), "Nonfinite force");
	if (auto* b = body(id); b && b->type == BodyType::Dynamic) {
		b->force += force;
		b->torque += (point - b->getPosition()).cross(force);
		if (force.squareMagnitude() > 0) {
			wake(id);
		}
	}
}
void PhysicWorld::applyImpulse(Id id, Vec3 impulse, Vec3 point) {
	require(finite(impulse) && finite(point), "Nonfinite impulse");
	if (auto* b = body(id); b && b->type == BodyType::Dynamic) {
		b->addVelocity(impulse * b->getInverseMass());
		b->addRotation(b->getInverseInertiaTensorWorld().transform((point - b->getPosition()).cross(impulse)));
		wake(id);
	}
}
void PhysicWorld::teleport(Id id, Vec3 p, MATH::QuaternionF q) {
	require(finite(p) && finite(q), "Invalid teleport");
	if (auto* b = body(id)) {
		b->setPosition(p);
		b->setOrientation(q);
		b->calculateDerivedData();
		for (auto& [jointId, joint] : mJoints) {
			if (joint.bodyA == id || joint.bodyB == id) {
				joint.anchorImpulse = {};
				joint.previousStep = 0;
			}
		}
		wake(id);
	}
}
void PhysicWorld::setVelocity(Id id, Vec3 v, Vec3 angular) {
	require(finite(v) && finite(angular), "Invalid velocity");
	if (auto* b = body(id); b && b->type == BodyType::Dynamic) {
		b->setVelocity(v);
		b->setRotation(angular);
		wake(id);
	}
}
void PhysicWorld::solveRigid(float dt, bool jointIteration) {
	auto& broad = *mPipeline.broadPhase;
	broad.reset(settings.cellSize);
	for (const auto& [id, b] : mBodies) {
		broad.upsert(id, COLLISION::bounds(geometry(b)));
	}
	std::vector<std::pair<Id, Id>> pairs;
	broad.pairs(pairs);
	PHYSICS_COMMON::canonicalPairs(pairs);
	mContactCount = 0;
	for (auto [aid, bid] : pairs) {
		if (!mBodies.contains(aid) || !mBodies.contains(bid)) {
			throw std::runtime_error("Broad phase returned unknown proxy");
		}
		auto& a = mBodies.at(aid);
		auto& b = mBodies.at(bid);
		if ((a.type == BodyType::Static && b.type == BodyType::Static) || !(a.layer & b.mask) || !(b.layer & a.mask) || !collisionEnabled(aid, bid)) {
			continue;
		}
		if (!COLLISION::bounds(geometry(a)).overlaps(COLLISION::bounds(geometry(b)))) {
			continue;
		}
		auto manifold = detect(geometry(a), geometry(b));
		if (mContactCount + manifold.count > settings.maximumContacts) {
			throw std::runtime_error("3D contact buffer exhausted (maximumContacts budget exceeded)");
		}
		if (mContactCount + manifold.count > mContacts.size()) {
			const auto required = mContactCount + manifold.count;
			if (required > settings.maximumContacts) {
				throw std::runtime_error("3D contact buffer exhausted (maximumContacts budget exceeded)");
			}
			mContacts.resize(std::min<size_t>(settings.maximumContacts, std::max(required, mContacts.size() * 2)));
		}
		for (int i = 0; i < manifold.count; ++i) {
			auto& c = mContacts[mContactCount++];
			c.contactPoint = manifold.points[i].position;
			c.penetration = manifold.points[i].penetration;
			c.contactNormal = -manifold.normal; // Legacy solver uses B-to-A.
			c.setBodyData(&a, &b, std::sqrt(a.friction * b.friction), std::max(a.restitution, b.restitution));
			const auto relative = a.getVelocity() + a.getRotation().cross(c.contactPoint - a.getPosition()) - b.getVelocity()
				- b.getRotation().cross(c.contactPoint - b.getPosition());
			c.closingSpeed = std::max(0.f, -relative.dot(c.contactNormal));
		}
	}
	mResolver.setIterations(jointIteration ? 1 : settings.velocityIterations, jointIteration ? 1 : settings.positionIterations);
	mResolver.resolveContacts(mContacts.data(), static_cast<unsigned>(mContactCount), dt);
	for (size_t index = 0; index < mContactCount; ++index) {
		const auto& contact = mContacts[index];
		if (contact.normalImpulse <= 0) {
			continue;
		}
		auto a = static_cast<const Body*>(contact.body[0])->id;
		auto b = static_cast<const Body*>(contact.body[1])->id;
		const auto pair = std::minmax(a, b);
		auto [found, inserted] = mImpactLookup.emplace(pair, mImpacts.size());
		if (inserted) {
			mImpacts.push_back({ a, b, contact.contactPoint, -contact.contactNormal, 0, 0 });
		}
		auto& impact = mImpacts[found->second];
		impact.normalImpulse += contact.normalImpulse;
		if (contact.closingSpeed >= impact.closingSpeed) {
			impact.closingSpeed = contact.closingSpeed;
			impact.point = contact.contactPoint;
			impact.normal = -contact.contactNormal;
		}
	}
	for (auto& [id, b] : mBodies) {
		b.calculateDerivedData();
	}
}
void PhysicWorld::step(float dt) {
	mImpacts.clear();
	mImpactLookup.clear();
	++mTick;
	require(std::isfinite(dt) && dt > 0 && finite(settings.gravity) && std::isfinite(settings.cellSize) && settings.cellSize > 0
			&& settings.substeps > 0 && settings.substeps <= 128 && settings.velocityIterations > 0 && settings.positionIterations > 0
			&& settings.particleIterations > 0 && settings.maximumContacts > 0 && settings.maximumContacts <= 1048576,
		"Invalid physics step/settings");
	for (auto& [id, f] : forces.fields()) {
		f.validate();
	}
	// Preserve inputs submitted through the inherited RigidBody API as well.
	for (auto& [id, b] : mBodies) {
		b.force += b.getAccumulatedForce();
		b.torque += b.getAccumulatedTorque();
		b.clearAccumulators();
	}
	float h = dt / settings.substeps;
	for (int substep = 0; substep < settings.substeps; ++substep) {
		for (auto& [id, b] : mBodies) {
			if (b.type == BodyType::Dynamic) {
				Vec3 generated, torque;
				bool shouldWake = false;
				for (auto& [fid, f] : forces.fields()) {
					if ((f.target && f.target != id) || !(f.targets & PHYSICS_COMMON::Rigid) || !(f.mask & b.layer)) {
						continue;
					}
					auto point = b.getPointInWorldSpace(f.localPoint), r = point - b.getPosition();
					auto force = f.evaluate(point, b.getVelocity() + b.getRotation().cross(r), b.getInverseMass(), mTime);
					generated += force;
					torque += r.cross(force);
					shouldWake = shouldWake || (f.wake && force.squareMagnitude() > 1e-12f);
				}
				if (shouldWake && !b.getAwake()) {
					wake(id);
				}
				b.clearAccumulators();
				if (b.getAwake()) {
					b.addForce(b.force + generated);
					b.addTorque(b.torque + torque);
				}
				b.setAcceleration(settings.gravity * b.gravityScale + b.extraAcceleration);
				b.integrate(h);
			}
		}
		if (mJoints.empty()) {
			solveRigid(h);
		}
		else {
			for (auto& [id, joint] : mJoints) {
				warmStartJoint(joint, mBodies.at(joint.bodyA), mBodies.at(joint.bodyB), h);
			}
			const int iterations = std::max(settings.velocityIterations, settings.positionIterations);
			for (int iteration = 0; iteration < iterations; ++iteration) {
				for (auto& [id, joint] : mJoints) {
					auto& a = mBodies.at(joint.bodyA);
					auto& b = mBodies.at(joint.bodyB);
					if (iteration < settings.velocityIterations) {
						solveJointVelocity(joint, a, b, h);
					}
					if (iteration < settings.positionIterations) {
						solveJointPosition(joint, a, b);
					}
				}
				// Rebuild manifolds after projection; contacts and joints share every iteration.
				solveRigid(h, true);
			}
		}
		stepParticles(h);
		mTime += h;
	}
	for (auto& [id, b] : mBodies) {
		b.force = {};
		b.torque = {};
	}
}
void PhysicWorld::debugDraw(const DebugDraw& draw) const {
	if (draw.line) {
		for (const auto& [id, joint] : mJoints) {
			const auto& a = mBodies.at(joint.bodyA);
			const auto& b = mBodies.at(joint.bodyB);
			const auto anchor = a.getPointInWorldSpace(joint.anchorA);
			draw.line(anchor, b.getPointInWorldSpace(joint.anchorB), 0xff4050ff);
			const auto frame = a.getOrientation() * joint.frameA;
			draw.line(anchor, anchor + frame * Vec3 { .15f, 0, 0 }, 0xff4040ff);
			draw.line(anchor, anchor + frame * Vec3 { 0, .15f, 0 }, 0x40ff40ff);
			draw.line(anchor, anchor + frame * Vec3 { 0, 0, .15f }, 0x4040ffff);
		}
	}
	for (auto& [id, b] : mBodies) {
		if (b.shape.kind == Shape::Kind::Sphere) {
			if (draw.sphere) {
				draw.sphere(b.getPosition(), b.shape.radius, 0x50e080ff);
			}
		}
		else if (b.shape.kind == Shape::Kind::Convex && draw.line) {
			for (const auto& edge : b.shape.polyhedron->edges) {
				draw.line(b.getPointInWorldSpace(b.shape.polyhedron->vertices[edge[0]]),
					b.getPointInWorldSpace(b.shape.polyhedron->vertices[edge[1]]), 0x50e080ff);
			}
		}
		else if (draw.line) {
			std::array<Vec3, 8> v;
			for (int i = 0; i < 8; ++i) {
				v[i] = b.getPointInWorldSpace(Vec3(i & 1 ? b.shape.halfSize.x : -b.shape.halfSize.x, i & 2 ? b.shape.halfSize.y : -b.shape.halfSize.y,
					i & 4 ? b.shape.halfSize.z : -b.shape.halfSize.z));
			}
			for (int i = 0; i < 8; ++i) {
				for (int axis = 0; axis < 3; ++axis) {
					if (!(i & (1 << axis))) {
						draw.line(v[i], v[i | (1 << axis)], 0x50e080ff);
					}
				}
			}
		}
	}
	for (auto& [id, s] : mSoft) {
		if (draw.line) {
			for (auto l : s.links) {
				draw.line(s.particles[l.a].position, s.particles[l.b].position, 0xe080ffff);
			}
		}
		if (draw.triangle) {
			for (auto t : s.triangles) {
				draw.triangle(s.particles[t[0]].position, s.particles[t[1]].position, s.particles[t[2]].position, 0xe080ffff);
			}
		}
		if (draw.sphere) {
			for (auto p : s.particles) {
				draw.sphere(p.position, s.radius, 0xe080ffff);
			}
		}
	}
	if (draw.sphere) {
		for (auto& [id, f] : mFluids) {
			for (auto p : f.particles) {
				draw.sphere(p.position, f.radius, 0x60b0ffff);
			}
		}
	}
}
}
