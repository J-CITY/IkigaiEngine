#pragma once
#include "physicWorld.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::PHYSICS::CHARACTER {
// Query-only upright capsule. Position is the bottom of the feet, not the centre.
struct Capsule {
	Vec3 feet {};
	float radius = .3f, height = 1.8f;
};
struct Hit {
	Id body = 0;
	float fraction = 1, distance = 0;
	Vec3 point {}, normal { 0, 1, 0 };
	explicit operator bool() const {
		return body != 0;
	}
};
struct Filter {
	std::uint32_t layer = 1, mask = ~0u;
	Id ignore = 0;
	bool staticsOnly = true;
	bool accepts(const Body& b) const {
		return b.id != ignore && (!staticsOnly || b.type == BodyType::Static) && (b.layer & mask) && (layer & b.mask);
	}
};
inline float length(Vec3 v) {
	return std::sqrt(v.dot(v));
}
inline Vec3 unit(Vec3 v, Vec3 fallback = { 0, 1, 0 }) {
	const float n = length(v);
	return n > 1e-7f ? v / n : fallback;
}
inline Vec3 clamp(Vec3 v, Vec3 h) {
	return { std::clamp(v.x, -h.x, h.x), std::clamp(v.y, -h.y, h.y), std::clamp(v.z, -h.z, h.z) };
}
struct Separation {
	float gap;
	Vec3 normal, point;
};
Separation separation(const Capsule& capsule, const Body& body);
Hit sweep(const World& world, Capsule capsule, Vec3 displacement, Filter filter = {}, float skin = .002f, float minNormalY = -2);
bool overlaps(const World& world, Capsule capsule, Filter filter = {}, float tolerance = .001f);
Hit raycast(const World& world, Vec3 origin, Vec3 delta, Filter filter = {});

struct TraversalPlan {
	Id obstacle = 0;
	Vec3 start {}, raised {}, end {};
	bool vault = false;
	Vec3 ledge {};
	explicit operator bool() const {
		return obstacle != 0;
	}
};
inline bool clearPath(const World& world, Capsule c, Vec3 end, Filter filter = {}) {
	return !sweep(world, c, end - c.feet, filter);
}
inline TraversalPlan planTraversal(const World& world, Capsule capsule, Vec3 direction, Filter filter = {}) {
	if (length(direction) < .1f) {
		return {};
	}
	direction = unit(Vec3 { direction.x, 0, direction.z }, { 0, 0, 0 });
	const auto feet = capsule.feet;
	const auto wall = raycast(world, feet + Vec3 { 0, .55f, 0 }, direction * 1.1f, filter);
	if (!wall || std::abs(wall.normal.y) > .3f || wall.normal.dot(direction) > -.6f) {
		return {};
	}
	const Vec3 over = wall.point + direction * .38f;
	const auto top = raycast(world, { over.x, feet.y + 2.65f, over.z }, { 0, -2.6f, 0 }, filter);
	if (!top || top.body != wall.body || top.normal.y < .95f) {
		return {};
	}
	const float height = top.point.y - feet.y;
	if (height < .45f || height > 2.35f) {
		return {};
	}
	const Vec3 raised { feet.x, top.point.y + .025f, feet.z };
	if (!clearPath(world, capsule, raised, filter)) {
		return {};
	}
	Vec3 end { over.x, top.point.y + .025f, over.z };
	bool vault = false;
	if (height < 1.0f) {
		const auto exitProbe = wall.point + direction * 1.5f;
		const auto ground = raycast(world, { exitProbe.x, top.point.y + .2f, exitProbe.z }, { 0, -height - 1.f, 0 }, filter);
		if (ground && ground.point.y < top.point.y - .3f && ground.normal.y > .7f) {
			const Vec3 exit { exitProbe.x, ground.point.y + .025f, exitProbe.z };
			auto check = capsule;
			check.feet = { exit.x, top.point.y + .025f, exit.z };
			if (!overlaps(world, check, filter) && clearPath(world, check, exit, filter)) {
				end = exit;
				vault = true;
			}
		}
	}
	const Vec3 aboveEnd { end.x, top.point.y + .025f, end.z };
	capsule.feet = raised;
	if (!clearPath(world, capsule, aboveEnd, filter)) {
		return {};
	}
	capsule.feet = end;
	if (overlaps(world, capsule, filter)) {
		return {};
	}
	return { wall.body, feet, raised, end, vault, { wall.point.x, top.point.y, wall.point.z } };
}
inline Vec3 slide(const World& world, Capsule c, Vec3 delta, Filter filter = {}) {
	for (int i = 0; i < 6 && length(delta) > 1e-5f; ++i) {
		const Hit h = sweep(world, c, delta, filter);
		if (!h) {
			c.feet += delta;
			break;
		}
		c.feet += delta * std::max(0.f, h.fraction - 1e-4f);
		delta = delta * (1 - h.fraction);
		const float into = delta.dot(h.normal);
		if (into < 0) {
			delta -= h.normal * into;
		}
		else {
			break;
		}
	}
	return c.feet;
}
class Motor {
public:
	Capsule capsule;
	Vec3 velocity {};
	bool grounded = false;
	float stepHeight = .3f, slopeCos = .7071f, jumpSpeed = 6, gravity = 18;
	float coyote = 0, jumpBuffer = 0, stepGrace = 0;
	bool steppedUp = false;
	Filter filter;
	void reset(Vec3 feet) {
		capsule.feet = feet;
		velocity = {};
		grounded = steppedUp = false;
		coyote = jumpBuffer = stepGrace = 0;
	}
	void move(const World& world, Vec3 wishVelocity, bool jump, float dt) {
		if (!(dt > 0) || !std::isfinite(dt)) {
			return;
		}
		steppedUp = false;
		const bool supportedBefore = grounded;
		// Recover from edits/spawn overlaps, with bounded work and displacement.
		for (int i = 0; i < 6; ++i) {
			bool corrected = false;
			for (const auto& [id, b] : world.bodies()) {
				if (filter.accepts(b)) {
					const auto s = separation(capsule, b);
					if (s.gap < -.001f) {
						capsule.feet += s.normal * std::min(-s.gap + .003f, .5f);
						corrected = true;
					}
				}
			}
			if (!corrected) {
				break;
			}
		}
		// Retain support across descending steps. A short probe used here made
		// the capsule fall for one tick before the longer ground snap could run.
		auto ground = sweep(world, capsule, { 0, -(supportedBefore && stepGrace <= 0 ? stepHeight + .09f : .09f), 0 }, filter, .002f, slopeCos);
		stepGrace = std::max(0.f, stepGrace - dt);
		grounded = velocity.y <= 0 && ((ground && ground.normal.y >= slopeCos) || stepGrace > 0);
		coyote = grounded ? .12f : std::max(0.f, coyote - dt);
		jumpBuffer = jump ? .14f : std::max(0.f, jumpBuffer - dt);
		if (grounded) {
			if (ground) {
				capsule.feet.y -= ground.distance;
			}
			velocity.y = 0;
		}
		if (coyote > 0 && jumpBuffer > 0) {
			velocity.y = jumpSpeed;
			grounded = false;
			coyote = jumpBuffer = stepGrace = 0;
		}
		const float response = (grounded ? 18.f : 5.f) * dt;
		velocity.x += (wishVelocity.x - velocity.x) * std::min(1.f, response);
		velocity.z += (wishVelocity.z - velocity.z) * std::min(1.f, response);
		if (!grounded) {
			velocity.y -= gravity * dt;
		}
		const Vec3 start = capsule.feet;
		const Vec3 horizontal { velocity.x * dt, 0, velocity.z * dt };
		Vec3 next = slide(world, capsule, horizontal, filter);
		const float wanted = length(horizontal);
		if (grounded && wanted > .0001f && length(Vec3 { next.x - start.x, 0, next.z - start.z }) < wanted * .85f) {
			const Vec3 up { 0, stepHeight, 0 };
			if (!sweep(world, capsule, up, filter)) {
				auto raised = capsule;
				raised.feet += up;
				raised.feet = slide(world, raised, horizontal, filter);
				const auto down = sweep(world, raised, { 0, -stepHeight - .09f, 0 }, filter);
				if (down && down.normal.y > .05f
					&& length(Vec3 { raised.feet.x - start.x, 0, raised.feet.z - start.z })
						> length(Vec3 { next.x - start.x, 0, next.z - start.z }) + .00001f) {
					// Keep the small lift while the capsule crosses the rounded
					// edge of a step. Do not snap back to the lower floor.
					next = raised.feet;
					steppedUp = true;
					if (down.normal.y >= slopeCos) {
						next.y -= down.distance;
					}
					else {
						stepGrace = .2f;
					}
				}
			}
		}
		capsule.feet = next;
		const Vec3 vertical { 0, velocity.y * dt, 0 };
		const auto vhit = sweep(world, capsule, vertical, filter);
		capsule.feet += vertical * (vhit ? std::max(0.f, vhit.fraction - 1e-4f) : 1.f);
		if (vhit) {
			if (velocity.y < 0 && vhit.normal.y >= slopeCos) {
				grounded = true;
			}
			velocity.y = 0;
		}
		if (grounded) {
			const auto snap = sweep(world, capsule, { 0, -(stepGrace > 0 ? .09f : stepHeight + .09f), 0 }, filter, .002f, slopeCos);
			if (snap && snap.normal.y >= slopeCos) {
				capsule.feet.y -= snap.distance;
			}
			else if (stepGrace <= 0) {
				grounded = false;
			}
		}
		velocity.x = (capsule.feet.x - start.x) / dt;
		velocity.z = (capsule.feet.z - start.z) / dt;
		steppedUp = steppedUp && grounded && capsule.feet.y - start.y > .015f;
	}
};
}
