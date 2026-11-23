#include "support/expect.h"

#include "physics2dModule/physics2d.h"

#include <cmath>
#include <stdexcept>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectNear;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using IKIGAI::PHYSICS2D::BodyDef;
using IKIGAI::PHYSICS2D::BodyType;
using IKIGAI::PHYSICS2D::JointDef;
using IKIGAI::PHYSICS2D::World;

float length(IKIGAI::PHYSICS2D::Vec2 value) {
	return std::sqrt(value.x * value.x + value.y * value.y);
}

void checkBodies() {
	World falling;
	BodyDef dropped;
	dropped.position = { 0.f, 5.f };
	const auto id = falling.createBody(dropped);
	falling.step(1.f / 60.f);
	const auto* body = falling.body(id);
	expect(body != nullptr && body->position.y < 5.f && falling.simulationTime() > 0.0, "physics2d.gravity");

	BodyDef fixed;
	fixed.type = BodyType::Static;
	fixed.position = { 3.f, 4.f };
	const auto staticId = falling.createBody(fixed);
	falling.step(1.f / 60.f);
	const auto* staticBody = falling.body(staticId);
	require(staticBody != nullptr, "physics2d.static.exists");
	expect(staticBody->position.y == 4.f, "physics2d.static");

	World overlap;
	overlap.settings.gravity = {};
	BodyDef left;
	left.type = BodyType::Static;
	BodyDef right;
	right.position = { 0.4f, 0.f };
	overlap.createBody(left);
	overlap.createBody(right);
	overlap.step(1.f / 60.f);
	expect(!overlap.contacts().empty(), "physics2d.overlap");
	World apart;
	apart.settings.gravity = {};
	BodyDef origin;
	origin.type = BodyType::Static;
	BodyDef distant = origin;
	distant.position = { 5.f, 0.f };
	const auto originId = apart.createBody(origin);
	const auto distantId = apart.createBody(distant);
	IKIGAI::PHYSICS2D::Contact contact;
	const auto* originBody = apart.body(originId);
	const auto* distantBody = apart.body(distantId);
	require(originBody && distantBody, "physics2d.separated.bodiesExist");
	expect(!IKIGAI::PHYSICS2D::collide(*originBody, *distantBody, contact), "physics2d.separated");

	World motion;
	motion.settings.gravity = {};
	BodyDef pushed;
	pushed.position = { 0.f, 0.f };
	const auto pushedId = motion.createBody(pushed);
	const auto* pushedBody = motion.body(pushedId);
	require(pushedBody != nullptr, "physics2d.pushed.exists");
	motion.applyImpulse(pushedId, { 1.f, 0.f }, pushed.position);
	motion.step(1.f / 60.f);
	expect(pushedBody->velocity.x > 0.f, "physics2d.impulse");
	motion.teleport(pushedId, { 8.f, 2.f }, 0.f);
	expect(pushedBody->position.x == 8.f, "physics2d.teleport");
	motion.setVelocity(pushedId, { 0.f, 3.f }, 0.f);
	expect(pushedBody->velocity.y == 3.f, "physics2d.velocity");
	expectThrows<std::invalid_argument>([&] { motion.step(0.f); }, "physics2d.invalidStep");
}

void checkDistanceJoint() {
	World linked;
	linked.settings.gravity = { 0.f, -9.81f };
	BodyDef anchor;
	anchor.type = BodyType::Static;
	BodyDef bob;
	bob.position = { 0.f, 2.f };
	const auto anchorId = linked.createBody(anchor);
	const auto bobId = linked.createBody(bob);
	JointDef joint;
	joint.a = anchorId;
	joint.b = bobId;
	joint.distance = 1.f;
	const auto jointId = linked.createJoint(joint);
	require(linked.joints().contains(jointId), "physics2d.distanceJoint.exists");
	for (int step = 0; step < 30; ++step) {
		linked.step(1.f / 60.f);
	}
	const auto* fixed = linked.body(anchorId);
	const auto* moving = linked.body(bobId);
	require(fixed && moving, "physics2d.distanceJoint.bodiesExist");
	const auto distance = length(moving->position - fixed->position);
	expectNear(distance, 1.f, 0.05f, "physics2d.distanceJoint.restLengthUnderLoad");
	linked.destroyBody(bobId);
	expect(linked.body(bobId) == nullptr && !linked.joints().contains(jointId), "physics2d.destroy.removesJoint");
	linked.clear();
	expect(linked.bodies().empty(), "physics2d.clear");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "basics", checkBodies }, { "distance_joint", checkDistanceJoint } });
}
