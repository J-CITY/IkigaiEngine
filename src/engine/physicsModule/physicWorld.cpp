#include "physicWorld.h"
#include "collisionModule/compoundCollider.h"
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
// Reuse storage across ticks and copy only state the solver can mutate.
struct PhysicWorld::TickCheckpoint {
	struct BodyState {
		Body* body = nullptr;
		RigidBody rigid {};
		Vec3 force {};
		Vec3 torque {};
	};
	struct JointState {
		RigidJoint* joint = nullptr;
		Vec3 impulse {};
		float previousStep = 0;
	};
	struct SoftState {
		SoftBody* body = nullptr;
		std::vector<Particle> particles {};
		std::vector<Vec3> previous {};
		std::vector<float> linkLambda {};
		std::vector<float> volumeLambda {};
	};
	struct FluidState {
		Fluid* body = nullptr;
		std::vector<Particle> particles {};
		std::vector<Vec3> previous {};
	};
	std::vector<BodyState> bodies {};
	std::vector<JointState> joints {};
	std::vector<SoftState> softBodies {};
	std::vector<FluidState> fluids {};
	std::vector<std::pair<Attachment*, float>> attachments {};
	std::vector<Contact> contacts {};
	std::vector<ImpactEvent> impacts {};
	double time = 0;

	void capture(PhysicWorld& world) {
		bodies.clear();
		bodies.reserve(world.mBodies.size());
		for (auto& [id, b] : world.mBodies) {
			bodies.push_back({ &b, b, b.force, b.torque });
		}
		joints.clear();
		joints.reserve(world.mJoints.size());
		for (auto& [id, joint] : world.mJoints) {
			joints.push_back({ &joint, joint.anchorImpulse, joint.previousStep });
		}
		attachments.clear();
		// Particle solves run after rigid contacts. A single substep cannot mutate them before overflow.
		const bool saveParticles = world.settings.substeps > 1;
		softBodies.resize(saveParticles ? world.mSoft.size() : 0);
		fluids.resize(saveParticles ? world.mFluids.size() : 0);
		if (saveParticles) {
			auto nextSoft = softBodies.begin();
			for (auto& [id, body] : world.mSoft) {
				auto& state = *nextSoft++;
				state.body = &body;
				state.particles = body.particles;
				state.previous = body.previous;
				state.linkLambda = body.linkLambda;
				state.volumeLambda = body.volumeLambda;
			}
			auto nextFluid = fluids.begin();
			for (auto& [id, body] : world.mFluids) {
				auto& state = *nextFluid++;
				state.body = &body;
				state.particles = body.particles;
				state.previous = body.previous;
			}
			attachments.reserve(world.mAttachments.size());
			for (auto& [id, attachment] : world.mAttachments) {
				attachments.emplace_back(&attachment, attachment.lambda);
			}
		}
		const auto currentContacts = world.contacts();
		contacts.assign(currentContacts.begin(), currentContacts.end());
		impacts = world.mImpacts;
		time = world.mTime;
	}

	void restore(PhysicWorld& world) const {
		// Preserve map nodes and particle storage referenced by contacts and existing callers.
		for (const auto& state : bodies) {
			static_cast<RigidBody&>(*state.body) = state.rigid;
			state.body->force = state.force;
			state.body->torque = state.torque;
		}
		for (const auto& state : joints) {
			state.joint->anchorImpulse = state.impulse;
			state.joint->previousStep = state.previousStep;
		}
		for (const auto& state : softBodies) {
			std::ranges::copy(state.particles, state.body->particles.begin());
			std::ranges::copy(state.previous, state.body->previous.begin());
			std::ranges::copy(state.linkLambda, state.body->linkLambda.begin());
			std::ranges::copy(state.volumeLambda, state.body->volumeLambda.begin());
		}
		for (const auto& state : fluids) {
			std::ranges::copy(state.particles, state.body->particles.begin());
			std::ranges::copy(state.previous, state.body->previous.begin());
		}
		for (const auto& [attachment, lambda] : attachments) {
			attachment->lambda = lambda;
		}
		std::ranges::copy(contacts, world.mContacts.begin());
		world.mContactCount = contacts.size();
		world.mImpacts = impacts;
		world.mImpactLookup.clear();
		world.mTime = time;
		world.refreshBroadPhase();
	}
};
ContactBudgetExceeded::ContactBudgetExceeded(unsigned required, unsigned maximum)
	: std::runtime_error("3D contact buffer exhausted (maximumContacts budget exceeded)")
	, requiredContacts(required)
	, maximumContacts(maximum) { }
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
PhysicWorld::PhysicWorld(CollisionPipeline pipeline)
	: PhysicWorld(Settings {}, std::move(pipeline)) { }
PhysicWorld::~PhysicWorld() = default;
COLLISION::Manifold3D PhysicWorld::detect(const COLLISION::ShapeView3D& a, const COLLISION::ShapeView3D& b) const {
	COLLISION::Manifold3D m;
	auto status = mPipeline.narrowPhase->collide(a, b, m);
	if (status == COLLISION::Status::Unsupported) {
		throw std::runtime_error("Narrow phase does not support shape pair");
	}
	if (status == COLLISION::Status::Separated) {
		return {};
	}
	if (status != COLLISION::Status::Contact || m.count < 1 || m.count > 4 || !finite(m.normal)
		|| std::abs(m.normal.squareMagnitude() - 1) > 0.001f) {
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
	require(d.shape.kind == Shape::Kind::Sphere || d.shape.kind == Shape::Kind::Box || d.shape.kind == Shape::Kind::Convex
			|| d.shape.kind == Shape::Kind::Compound,
		"Invalid shape kind");
	Body b;
	b.shape = d.shape.kind == Shape::Kind::Sphere ? Shape::sphere(d.shape.radius)
		: d.shape.kind == Shape::Kind::Box		  ? Shape::box(d.shape.halfSize)
		: d.shape.kind == Shape::Kind::Convex	  ? Shape::convex(d.shape.polyhedron)
												  : Shape::compound(d.shape.compoundCollider);
	if (d.shape.kind == Shape::Kind::Convex) {
		require(d.shape.polyhedron->center.magnitude() < 1e-3f, "Convex body vertices must be centered at COM");
	}
	if (d.shape.kind == Shape::Kind::Compound && d.type == BodyType::Dynamic) {
		require(d.compoundInertiaPerUnitMass.has_value(), "Dynamic compound requires inertia about its COM");
		const auto& tensor = *d.compoundInertiaPerUnitMass;
		std::array<double, 9> v {};
		double scale = 0;
		for (unsigned index = 0; index < 9; ++index) {
			const auto value = tensor.getData()[index];
			require(std::isfinite(value) && std::isfinite(value * d.mass), "Nonfinite compound inertia");
			v[index] = value;
			scale = std::max(scale, std::abs(v[index]));
		}
		for (const auto pair : { std::pair { 1u, 3u }, std::pair { 2u, 6u }, std::pair { 5u, 7u } }) {
			require(std::abs(v[pair.first] - v[pair.second]) <= scale * 1e-6, "Compound inertia must be symmetric");
			v[pair.first] = v[pair.second] = (v[pair.first] + v[pair.second]) * .5;
		}
		const double minor = double(v[0]) * v[4] - double(v[1]) * v[3];
		const double determinant = double(v[0]) * (double(v[4]) * v[8] - double(v[5]) * v[7])
			- double(v[1]) * (double(v[3]) * v[8] - double(v[5]) * v[6]) + double(v[2]) * (double(v[3]) * v[7] - double(v[4]) * v[6]);
		require(v[0] > 0 && minor > 0 && determinant > 0, "Compound inertia must be positive definite");
		// Invert in double precision: small fragments can underflow a float determinant.
		MATH::Matrix3f inverse {};
		for (unsigned row = 0; row < 3; ++row) {
			for (unsigned column = 0; column < 3; ++column) {
				const auto firstRow = (column + 1) % 3;
				const auto secondRow = (column + 2) % 3;
				const auto firstColumn = (row + 1) % 3;
				const auto secondColumn = (row + 2) % 3;
				const double cofactor = double(v[firstRow * 3 + firstColumn]) * v[secondRow * 3 + secondColumn]
					- double(v[firstRow * 3 + secondColumn]) * v[secondRow * 3 + firstColumn];
				inverse(row, column) = static_cast<float>(cofactor / determinant / d.mass);
				require(std::isfinite(inverse(row, column)), "Compound inverse inertia overflow");
			}
		}
		b.setInverseInertiaTensor(inverse);
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
		if (d.shape.kind != Shape::Kind::Compound) {
			b.setInertiaTensor(d.shape.kind == Shape::Kind::Convex ? d.shape.polyhedron->inertia * d.mass
																   : MATH::Matrix3f(inertia.x, 0, 0, 0, inertia.y, 0, 0, 0, inertia.z));
		}
	}
	b.calculateDerivedData();
	mBodies.emplace(b.id, b);
	return b.id;
}
std::vector<Id> PhysicWorld::createBodies(std::span<const BodyDef> definitions) {
	std::vector<Id> result {};
	result.reserve(definitions.size());
	try {
		for (const auto& definition : definitions) {
			result.push_back(createBody(definition));
		}
	} catch (...) {
		// createBody does not insert into broad phase or constraints; candidates were never simulated.
		for (auto id : result) {
			mBodies.erase(id);
		}
		throw;
	}
	return result;
}
void PhysicWorld::refreshBroadPhase() {
	auto& broad = *mPipeline.broadPhase;
	broad.reset(settings.cellSize);
	for (const auto& [id, body] : mBodies) {
		broad.upsert(id, COLLISION::bounds(geometry(body)));
	}
}
std::vector<Id> PhysicWorld::queryBodies(const MATH::GEOMETRY::Aabb<3>& bounds) {
	bounds.validate();
	// Queries may follow teleport/create/projection. Refresh proxies from authoritative poses.
	refreshBroadPhase();
	std::vector<Id> result {};
	mPipeline.broadPhase->query(bounds, result);
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
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
	if (body(definition.bodyA)->type == BodyType::Dynamic) {
		wake(definition.bodyA);
	}
	if (body(definition.bodyB)->type == BodyType::Dynamic) {
		wake(definition.bodyB);
	}
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
		const auto bodyA = found->second.bodyA;
		const auto bodyB = found->second.bodyB;
		mJoints.erase(found);
		if (body(bodyA)->type == BodyType::Dynamic) {
			wake(bodyA);
		}
		if (body(bodyB)->type == BodyType::Dynamic) {
			wake(bodyB);
		}
	}
}
const std::map<Id, RigidJoint>& PhysicWorld::joints() const {
	return mJoints;
}
bool PhysicWorld::collisionEnabled(Id a, Id b) const {
	return !mJointCollisionExclusions.contains(std::minmax(a, b));
}
void PhysicWorld::destroyBody(Id id) {
	const auto found = mBodies.find(id);
	if (found == mBodies.end()) {
		return;
	}
	const auto* removed = &found->second;
	// Wake before removing edges. Static bodies connect only the islands they directly support.
	wake(id);
	mPipeline.broadPhase->remove(id);
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
	// Keep the other contacts available for wake traversal if another body is removed this tick.
	const auto end = mContacts.begin() + mContactCount;
	const auto remaining = std::remove_if(
		mContacts.begin(), end, [removed](const Contact& contact) { return contact.body[0] == removed || contact.body[1] == removed; });
	mContactCount = static_cast<std::size_t>(remaining - mContacts.begin());
	mBodies.erase(found);
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
	mTickCheckpoint.reset();
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
		for (const auto& c : contacts()) {
			for (int i = 0; i < 2; ++i) {
				if (c.body[i] == b && c.body[1 - i]) {
					pending.push_back(static_cast<const Body*>(c.body[1 - i])->id);
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
	refreshBroadPhase();
	std::vector<std::pair<Id, Id>> pairs;
	mPipeline.broadPhase->pairs(pairs);
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
		const auto viewA = geometry(a);
		const auto viewB = geometry(b);
		const auto countA = COLLISION::compoundLeafCount(viewA);
		const auto countB = COLLISION::compoundLeafCount(viewB);
		const bool compound = a.shape.kind == Shape::Kind::Compound || b.shape.kind == Shape::Kind::Compound;
		std::array<MATH::GEOMETRY::Aabb<3>, COLLISION::CompoundCollider3D::MaxChildren> boundsB {};
		if (compound) {
			for (size_t childB = 0; childB < countB; ++childB) {
				boundsB[childB] = COLLISION::bounds(COLLISION::compoundLeafView(viewB, childB));
			}
		}
		for (size_t childA = 0; childA < countA; ++childA) {
			const auto leafA = COLLISION::compoundLeafView(viewA, childA);
			const auto boundsA = compound ? COLLISION::bounds(leafA) : MATH::GEOMETRY::Aabb<3> {};
			for (size_t childB = 0; childB < countB; ++childB) {
				if (compound && !boundsA.overlaps(boundsB[childB])) {
					continue;
				}
				const auto leafB = COLLISION::compoundLeafView(viewB, childB);
				auto manifold = detect(leafA, leafB);
				const auto required = mContactCount + manifold.count;
				if (required > settings.maximumContacts) {
					throw ContactBudgetExceeded(static_cast<unsigned>(required), settings.maximumContacts);
				}
				if (required > mContacts.size()) {
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
	require(std::isfinite(dt) && dt > 0 && finite(settings.gravity) && std::isfinite(settings.cellSize) && settings.cellSize > 0
			&& settings.substeps > 0 && settings.substeps <= 128 && settings.velocityIterations > 0 && settings.positionIterations > 0
			&& settings.particleIterations > 0 && settings.maximumContacts > 0 && settings.maximumContacts <= 1048576,
		"Invalid physics step/settings");
	for (auto& [id, f] : forces.fields()) {
		f.validate();
	}
	if (!mTickCheckpoint) {
		mTickCheckpoint = std::make_unique<TickCheckpoint>();
	}
	mTickCheckpoint->capture(*this);
	try {
		advance(dt);
	} catch (const ContactBudgetExceeded&) {
		mTickCheckpoint->restore(*this);
		throw;
	}
	++mTick;
}
void PhysicWorld::advance(float dt) {
	mImpacts.clear();
	mImpactLookup.clear();
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
		for (size_t child = 0; child < COLLISION::compoundLeafCount(geometry(b)); ++child) {
			const auto leaf = COLLISION::compoundLeafView(geometry(b), child);
			if (leaf.shape.kind == Shape::Kind::Sphere) {
				if (draw.sphere) {
					draw.sphere(leaf.position, leaf.shape.radius, 0x50e080ff);
				}
			}
			else if (leaf.shape.kind == Shape::Kind::Convex && draw.line) {
				for (const auto& edge : leaf.shape.polyhedron->edges) {
					draw.line(leaf.world(leaf.shape.polyhedron->vertices[edge[0]]), leaf.world(leaf.shape.polyhedron->vertices[edge[1]]), 0x50e080ff);
				}
			}
			else if (draw.line) {
				std::array<Vec3, 8> v;
				for (int i = 0; i < 8; ++i) {
					v[i] = leaf.world(Vec3(i & 1 ? leaf.shape.halfSize.x : -leaf.shape.halfSize.x,
						i & 2 ? leaf.shape.halfSize.y : -leaf.shape.halfSize.y, i & 4 ? leaf.shape.halfSize.z : -leaf.shape.halfSize.z));
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
