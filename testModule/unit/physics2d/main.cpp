#include "support/expect.h"

#include "physics2dModule/physics2d.h"

using IKIGAI::TEST::expect;

int main() {
	using IKIGAI::PHYSICS2D::BodyDef;
	using IKIGAI::PHYSICS2D::BodyType;
	using IKIGAI::PHYSICS2D::World;

	World falling;
	BodyDef dropped;
	dropped.position = { 0.f, 5.f };
	const auto id = falling.createBody(dropped);
	falling.step(1.f / 60.f);
	const auto* body = falling.body(id);
	expect(body != nullptr && body->position.y < 5.f, "physics2d.gravity");

	World overlap;
	overlap.settings.gravity = {};
	BodyDef left;
	left.type = BodyType::Static;
	left.position = { 0.f, 0.f };
	BodyDef right;
	right.position = { 0.4f, 0.f };
	overlap.createBody(left);
	overlap.createBody(right);
	overlap.step(1.f / 60.f);
	expect(!overlap.contacts().empty(), "physics2d.overlap");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
