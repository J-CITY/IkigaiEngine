#include "support/expect.h"

#include "physicsModule/physicWorld.h"

#include <algorithm>
#include <stdexcept>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using IKIGAI::PHYSICS::BodyDef;
using IKIGAI::PHYSICS::BodyType;
using IKIGAI::PHYSICS::PhysicWorld;
using IKIGAI::PHYSICS::Shape;

void checkBodies() {
	using IKIGAI::MATH::GEOMETRY::Aabb;

	PhysicWorld falling;
	BodyDef dropped;
	dropped.shape = Shape::sphere(0.5f);
	dropped.position = { 0.f, 5.f, 0.f };
	const auto id = falling.createBody(dropped);
	falling.step(1.f / 60.f);
	const auto* body = falling.body(id);
	expect(body != nullptr && body->getPosition().y < 5.f && falling.simulationTime() > 0.0, "physics.gravity");

	BodyDef fixed = dropped;
	fixed.type = BodyType::Static;
	fixed.position = { 4.f, 2.f, 0.f };
	const auto staticId = falling.createBody(fixed);
	falling.step(1.f / 60.f);
	const auto* staticBody = falling.body(staticId);
	require(staticBody != nullptr, "physics.static.exists");
	expect(staticBody->getPosition().y == 2.f, "physics.static");

	PhysicWorld overlap;
	overlap.settings.gravity = {};
	BodyDef left;
	left.shape = Shape::sphere(0.5f);
	left.type = BodyType::Static;
	BodyDef right = left;
	right.type = BodyType::Dynamic;
	right.position = { 0.4f, 0.f, 0.f };
	overlap.createBody(left);
	const auto dynamicId = overlap.createBody(right);
	overlap.step(1.f / 60.f);
	const auto* moved = overlap.body(dynamicId);
	expect(moved != nullptr && moved->getPosition().x > 0.4f, "physics.separation");

	PhysicWorld motion;
	motion.settings.gravity = {};
	BodyDef pushed;
	pushed.shape = Shape::sphere(0.5f);
	pushed.mass = 2.f;
	const auto pushedId = motion.createBody(pushed);
	const auto* pushedBody = motion.body(pushedId);
	require(pushedBody != nullptr, "physics.pushed.exists");
	motion.applyImpulse(pushedId, { 1.f, 0.f, 0.f }, {});
	expectNear(pushedBody->getVelocity().x, 0.5f, "physics.impulse.velocityChange");
	motion.teleport(pushedId, { 3.f, 1.f, 0.f }, { 0.f, 0.f, 0.f, 1.f });
	expect(pushedBody->getPosition().x == 3.f, "physics.teleport");

	Aabb<3> nearOrigin;
	nearOrigin.min = { -1.f, -1.f, -1.f };
	nearOrigin.max = { 1.f, 1.f, 1.f };
	Aabb<3> farAway;
	farAway.min = { 20.f, 20.f, 20.f };
	farAway.max = { 21.f, 21.f, 21.f };
	const auto near = motion.queryBodies(nearOrigin);
	const auto distant = motion.queryBodies(farAway);
	expect(std::find(near.begin(), near.end(), pushedId) == near.end(), "physics.query.missesTeleported");
	Aabb<3> aroundBody;
	aroundBody.min = { 2.f, 0.f, -1.f };
	aroundBody.max = { 4.f, 2.f, 1.f };
	const auto found = motion.queryBodies(aroundBody);
	expect(std::find(found.begin(), found.end(), pushedId) != found.end() && distant.empty(), "physics.query");

	expectThrows<std::invalid_argument>(
		[] {
			PhysicWorld world;
			BodyDef invalid;
			invalid.shape = Shape::sphere(0.5f);
			invalid.mass = 0.f;
			world.createBody(invalid);
		},
		"physics.invalidBody");
	expectThrows<std::invalid_argument>([] { PhysicWorld {}.step(0.f); }, "physics.invalidStep");
}

void checkHinge() {
	using IKIGAI::MATH::Vector3f;
	using IKIGAI::PHYSICS::RigidJointDef;
	using IKIGAI::PHYSICS::RigidJointKind;

	PhysicWorld linked;
	linked.settings.gravity = { 0.f, -9.81f, 3.f };
	BodyDef anchor;
	anchor.shape = Shape::sphere(0.2f);
	anchor.type = BodyType::Static;
	BodyDef bob = anchor;
	bob.type = BodyType::Dynamic;
	bob.position = { 0.f, 1.3f, 0.f };
	const auto anchorId = linked.createBody(anchor);
	const auto bobId = linked.createBody(bob);
	RigidJointDef joint;
	joint.bodyA = anchorId;
	joint.bodyB = bobId;
	joint.kind = RigidJointKind::Hinge;
	joint.anchorA = { 0.f, 1.f, 0.f };
	joint.damping = 0.f;
	joint.twistMin = -1.5f;
	joint.twistMax = 1.5f;
	const auto jointId = linked.createJoint(joint);
	require(linked.joints().contains(jointId), "physics.hinge.exists");
	linked.setVelocity(bobId, {}, { 1.f, 0.8f, 0.6f });
	for (int step = 0; step < 45; ++step) {
		linked.step(1.f / 60.f);
	}
	const auto* fixed = linked.body(anchorId);
	const auto* moving = linked.body(bobId);
	require(fixed && moving, "physics.hinge.bodiesExist");
	const auto separation = moving->getPointInWorldSpace(joint.anchorB) - fixed->getPointInWorldSpace(joint.anchorA);
	expectNear(separation.magnitude(), 0.f, 0.02f, "physics.hinge.anchorsCoincideUnderLoad");
	const auto axis = moving->getDirectionInWorldSpace(Vector3f { 1.f, 0.f, 0.f });
	expectNear(axis.x, 1.f, 0.01f, "physics.hinge.axisStaysAligned");
	const auto marker = moving->getDirectionInWorldSpace(Vector3f { 0.f, 1.f, 0.f });
	expect(marker.z > 0.35f, "physics.hinge.allowsTwist");
	linked.destroyBody(bobId);
	expect(linked.body(bobId) == nullptr && !linked.joints().contains(jointId), "physics.hinge.removedWithBody");
	linked.clear();
	expect(linked.body(anchorId) == nullptr, "physics.clear");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "basics", checkBodies }, { "hinge", checkHinge } });
}
