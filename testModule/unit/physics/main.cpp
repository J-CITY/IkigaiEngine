#include "support/expect.h"

#include "physicsModule/physicWorld.h"

#include <cmath>

using IKIGAI::TEST::expect;

int main() {
	using IKIGAI::PHYSICS::BodyDef;
	using IKIGAI::PHYSICS::BodyType;
	using IKIGAI::PHYSICS::PhysicWorld;
	using IKIGAI::PHYSICS::Shape;

	PhysicWorld falling;
	BodyDef dropped;
	dropped.shape = Shape::sphere(0.5f);
	dropped.position = { 0.f, 5.f, 0.f };
	const auto id = falling.createBody(dropped);
	falling.step(1.f / 60.f);
	const auto* body = falling.body(id);
	expect(body != nullptr && body->getPosition().y < 5.f, "physics.gravity");

	PhysicWorld overlap;
	overlap.settings.gravity = {};
	BodyDef left;
	left.shape = Shape::sphere(0.5f);
	left.type = BodyType::Static;
	left.position = { 0.f, 0.f, 0.f };
	BodyDef right = left;
	right.type = BodyType::Dynamic;
	right.position = { 0.4f, 0.f, 0.f };
	overlap.createBody(left);
	const auto dynamicId = overlap.createBody(right);
	overlap.step(1.f / 60.f);
	const auto* moved = overlap.body(dynamicId);
	// The velocity/position solver separates the pair inside this step, so the
	// contact list left behind can already be empty. The pose is the result.
	expect(moved != nullptr && std::fabs(moved->getPosition().x) > 0.4f, "physics.separation");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
