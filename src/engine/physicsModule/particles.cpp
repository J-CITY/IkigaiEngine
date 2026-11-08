#include "collisionModule/compoundCollider.h"
#include "physicWorld.h"
#include "spatialGrid.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace IKIGAI::PHYSICS {
namespace {
	constexpr float Pi = 3.14159265358979323846f;
	bool positive(float x) {
		return std::isfinite(x) && x > 0;
	}
	bool nonnegative(float x) {
		return std::isfinite(x) && x >= 0;
	}
	void require(bool ok, const char* message) {
		if (!ok) {
			throw std::invalid_argument(message);
		}
	}
	void checkParticles(const std::vector<Particle>& p, float radius) {
		require(!p.empty() && positive(radius), "Empty particle set or invalid radius");
		for (auto v : p) {
			require(PHYSICS_COMMON::finite(v.position) && PHYSICS_COMMON::finite(v.velocity) && nonnegative(v.inverseMass), "Invalid particle state");
		}
	}
	float volume(const std::vector<Particle>& p, Tetrahedron t) {
		return (p[t[1]].position - p[t[0]].position).dot((p[t[2]].position - p[t[0]].position).cross(p[t[3]].position - p[t[0]].position)) / 6;
	}
	Vec3 minimum(Vec3 a, Vec3 b) {
		return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
	}
	Vec3 maximum(Vec3 a, Vec3 b) {
		return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
	}
	// Closest point and barycentric weights, including edge/vertex Voronoi regions.
	Vec3 weights(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
		auto ab = b - a, ac = c - a, ap = p - a;
		float d1 = ab.dot(ap), d2 = ac.dot(ap);
		if (d1 <= 0 && d2 <= 0) {
			return { 1, 0, 0 };
		}
		auto bp = p - b;
		float d3 = ab.dot(bp), d4 = ac.dot(bp);
		if (d3 >= 0 && d4 <= d3) {
			return { 0, 1, 0 };
		}
		float vc = d1 * d4 - d3 * d2;
		if (vc <= 0 && d1 >= 0 && d3 <= 0) {
			float v = d1 / (d1 - d3);
			return { 1 - v, v, 0 };
		}
		auto cp = p - c;
		float d5 = ab.dot(cp), d6 = ac.dot(cp);
		if (d6 >= 0 && d5 <= d6) {
			return { 0, 0, 1 };
		}
		float vb = d5 * d2 - d1 * d6;
		if (vb <= 0 && d2 >= 0 && d6 <= 0) {
			float w = d2 / (d2 - d6);
			return { 1 - w, 0, w };
		}
		float va = d3 * d6 - d5 * d4;
		if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
			float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			return { 0, 1 - w, w };
		}
		float den = va + vb + vc;
		if (std::abs(den) < 1e-16f) {
			return { 1, 0, 0 };
		}
		return { 1 - (vb + vc) / den, vb / den, vc / den };
	}
	float kernel(Vec3 r, float h) {
		float q = std::max(0.0f, h * h - r.squareMagnitude());
		return 315.0f / (64 * Pi * std::pow(h, 9)) * q * q * q;
	}
	Vec3 gradient(Vec3 r, float h) {
		float q = std::max(0.0f, h * h - r.squareMagnitude());
		return r * (-945.0f / (32 * Pi * std::pow(h, 9)) * q * q);
	}
}
Id PhysicWorld::createSoftBody(const SoftBodyDef& d) {
	checkParticles(d.particles, d.radius);
	require(
		nonnegative(d.compliance) && nonnegative(d.volumeCompliance) && nonnegative(d.damping) && nonnegative(d.friction), "Invalid soft material");
	for (auto c : d.links) {
		require(c.a < d.particles.size() && c.b < d.particles.size() && c.a != c.b && positive(c.restLength) && nonnegative(c.compliance),
			"Invalid distance constraint");
	}
	for (auto t : d.triangles) {
		require(t[0] < d.particles.size() && t[1] < d.particles.size() && t[2] < d.particles.size(), "Invalid surface triangle");
		require(
			(d.particles[t[1]].position - d.particles[t[0]].position).cross(d.particles[t[2]].position - d.particles[t[0]].position).squareMagnitude()
				> 1e-16f,
			"Degenerate surface triangle");
	}
	SoftBody s;
	static_cast<SoftBodyDef&>(s) = d;
	for (auto t : d.tetrahedra) {
		std::set<std::size_t> unique(t.begin(), t.end());
		require(unique.size() == 4 && *unique.rbegin() < d.particles.size(), "Invalid tetrahedron indices");
		float v = volume(d.particles, t);
		require(v > 1e-10f, "Tetrahedron must have positive nonzero volume");
		s.restVolumes.push_back(v);
	}
	s.linkLambda.resize(s.links.size());
	s.volumeLambda.resize(s.tetrahedra.size());
	s.previous.resize(s.particles.size());
	s.id = mNextId++;
	mSoft.emplace(s.id, s);
	return s.id;
}
Id PhysicWorld::createCloth(const ClothDef& d) {
	require(d.width >= 2 && d.height >= 2 && d.width <= 256 && d.height <= 256 && positive(d.spacing) && positive(d.particleMass)
			&& nonnegative(d.stretchCompliance) && nonnegative(d.shearCompliance) && nonnegative(d.bendCompliance)
			&& PHYSICS_COMMON::finite(d.position) && PHYSICS_COMMON::finite(d.axisU) && PHYSICS_COMMON::finite(d.axisV)
			&& d.axisU.cross(d.axisV).squareMagnitude() > 1e-8f,
		"Invalid cloth grid");
	SoftBodyDef s;
	s.radius = d.radius;
	s.damping = d.damping;
	s.friction = d.friction;
	s.selfCollision = d.selfCollision;
	s.cloth = true;
	s.impermeable = d.impermeable;
	for (int y = 0; y < d.height; ++y) {
		for (int x = 0; x < d.width; ++x) {
			s.particles.push_back(
				{ d.position + d.axisU * (x * d.spacing) + d.axisV * (y * d.spacing), {}, d.pinTop && y == 0 ? 0 : 1 / d.particleMass });
		}
	}
	auto link = [&](int a, int b, float compliance) {
		s.links.push_back({ std::size_t(a), std::size_t(b), (s.particles[b].position - s.particles[a].position).magnitude(), compliance });
	};
	for (int y = 0; y < d.height; ++y) {
		for (int x = 0; x < d.width; ++x) {
			int i = y * d.width + x;
			if (x + 1 < d.width) {
				link(i, i + 1, d.stretchCompliance);
			}
			if (y + 1 < d.height) {
				link(i, i + d.width, d.stretchCompliance);
			}
			if (x + 1 < d.width && y + 1 < d.height) {
				link(i, i + d.width + 1, d.shearCompliance);
				link(i + 1, i + d.width, d.shearCompliance);
				s.triangles.push_back({ std::size_t(i), std::size_t(i + 1), std::size_t(i + d.width) });
				s.triangles.push_back({ std::size_t(i + 1), std::size_t(i + d.width + 1), std::size_t(i + d.width) });
			}
			// Distance bending across two adjacent cells; compliance is independent of stretch.
			if (x + 2 < d.width) {
				link(i, i + 2, d.bendCompliance);
			}
			if (y + 2 < d.height) {
				link(i, i + 2 * d.width, d.bendCompliance);
			}
		}
	}
	return createSoftBody(s);
}
Id PhysicWorld::createFluid(const FluidDef& d) {
	checkParticles(d.particles, d.radius);
	require(positive(d.smoothingRadius) && d.smoothingRadius > 2 * d.radius && positive(d.particleMass) && positive(d.restDensity)
			&& nonnegative(d.viscosity) && d.viscosity <= 1 && positive(d.relaxation),
		"Invalid fluid material");
	// Regions of one material share a pressure solve, including separate emitters.
	for (auto& [id, f] : mFluids) {
		require(f.smoothingRadius == d.smoothingRadius && f.particleMass == d.particleMass && f.restDensity == d.restDensity
				&& f.viscosity == d.viscosity && f.relaxation == d.relaxation,
			"This world supports one interacting fluid material");
	}
	Fluid f;
	static_cast<FluidDef&>(f) = d;
	for (auto& p : f.particles) {
		p.inverseMass = 1 / f.particleMass;
	}
	f.previous.resize(f.particles.size());
	f.id = mNextId++;
	mFluids.emplace(f.id, f);
	return f.id;
}
Id PhysicWorld::createAttachment(const AttachmentDef& d) {
	auto* s = softBody(d.soft);
	require(s && d.particle < s->particles.size() && (!d.body || body(d.body)) && PHYSICS_COMMON::finite(d.point) && nonnegative(d.compliance),
		"Invalid attachment");
	require(s->particles[d.particle].inverseMass > 0, "Unpin particle before attaching it to a moving body");
	Attachment a;
	static_cast<AttachmentDef&>(a) = d;
	a.id = mNextId++;
	mAttachments.emplace(a.id, a);
	return a.id;
}
void PhysicWorld::solveAttachments(float dt) {
	for (auto& [id, a] : mAttachments) {
		auto& p = mSoft.at(a.soft).particles[a.particle];
		auto* b = body(a.body);
		auto target = b ? b->getPointInWorldSpace(a.point) : a.point;
		auto delta = p.position - target;
		float length = delta.magnitude();
		if (length < 1e-8f) {
			continue;
		}
		auto n = delta * (1 / length);
		auto r = b ? target - b->getPosition() : Vec3 {};
		float w = b ? b->getInverseMass() : 0;
		auto angular = b ? b->getInverseInertiaTensorWorld().transform(r.cross(n)) : Vec3 {};
		float den = p.inverseMass + w + angular.cross(r).dot(n);
		float dl = PHYSICS_COMMON::multiplier(length, den, a.compliance, dt, a.lambda);
		p.position += n * (dl * p.inverseMass);
		if (b && w > 0) {
			b->setPosition(b->getPosition() - n * (dl * w));
			auto q = b->getOrientation();
			q.addScaledVector(angular, -dl);
			b->setOrientation(q);
			b->calculateDerivedData();
			b->addVelocity(n * (-dl * w / dt));
			b->addRotation(angular * (-dl / dt));
			b->setAwake();
		}
	}
}
void PhysicWorld::projectParticle(
	Particle& p, float radius, float friction, std::uint32_t layer, std::uint32_t mask, float dt, UTILS::SPATIAL::ISpatialIndex<3>& solids) {
	if (p.inverseMass == 0) {
		return;
	}
	std::vector<Id> candidates;
	solids.query(UTILS::SPATIAL::SpatialQuery<3, Vec3>::bounds(p.position - Vec3(radius), p.position + Vec3(radius)), candidates);
	std::sort(candidates.begin(), candidates.end());
	candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
	for (auto id : candidates) {
		auto& b = mBodies.at(id);
		if (!(layer & b.mask) || !(b.layer & mask)) {
			continue;
		}
		for (size_t child = 0; child < COLLISION::compoundLeafCount(geometry(b)); ++child) {
			auto particleShape = Shape::sphere(radius);
			auto manifold = detect({ particleShape, p.position }, COLLISION::compoundLeafView(geometry(b), child));
			if (!manifold.count || manifold.points[0].penetration <= 0) {
				continue;
			}
			auto n = -manifold.normal, point = manifold.points[0].position;
			float penetration = manifold.points[0].penetration;
			auto r = point - b.getPosition(), rn = r.cross(n), angular = b.getInverseInertiaTensorWorld().transform(rn);
			float inv = p.inverseMass + b.getInverseMass() + angular.cross(r).dot(n);
			if (inv < 1e-12f) {
				continue;
			}
			float dl = penetration / inv;
			p.position += n * (dl * p.inverseMass);
			if (b.type == BodyType::Dynamic) {
				b.setPosition(b.getPosition() - n * (dl * b.getInverseMass()));
				auto q = b.getOrientation();
				q.addScaledVector(angular, -dl);
				b.setOrientation(q);
				b.calculateDerivedData();
				b.addVelocity(n * (-dl * b.getInverseMass() / dt));
				b.addRotation(angular * (-dl / dt));
				b.setAwake();
			}
			auto relative = p.velocity - (b.getVelocity() + b.getRotation().cross(r));
			auto tangent = relative - n * relative.dot(n);
			float speed = tangent.magnitude();
			if (speed > 1e-8f) {
				auto t = tangent * (1 / speed);
				auto ra = b.getInverseInertiaTensorWorld().transform(r.cross(t));
				float eff = p.inverseMass + b.getInverseMass() + ra.cross(r).dot(t);
				float j = std::min(speed / eff, std::sqrt(friction * b.friction) * dl / dt);
				p.position -= t * (j * p.inverseMass * dt);
				if (b.type == BodyType::Dynamic) {
					b.setPosition(b.getPosition() + t * (j * b.getInverseMass() * dt));
					b.addVelocity(t * (j * b.getInverseMass()));
					b.addRotation(ra * j);
					b.calculateDerivedData();
				}
			}
			if (b.type == BodyType::Dynamic) {
				solids.upsert(id, COLLISION::bounds(geometry(b)));
			}
		}
	}
}
void PhysicWorld::stepParticles(float dt) {
	if (mSoft.empty() && mFluids.empty()) {
		return;
	}
	for (auto& [id, a] : mAttachments) {
		a.lambda = 0;
	}
	for (auto& [id, s] : mSoft) {
		std::fill(s.linkLambda.begin(), s.linkLambda.end(), 0);
		std::fill(s.volumeLambda.begin(), s.volumeLambda.end(), 0);
		std::vector<Vec3> wind(s.particles.size());
		if (s.cloth) {
			for (auto triangle : s.triangles) {
				auto &a = s.particles[triangle[0]], &b = s.particles[triangle[1]], &c = s.particles[triangle[2]];
				auto cross = (b.position - a.position).cross(c.position - a.position);
				float twiceArea = cross.magnitude();
				if (twiceArea < 1e-10f) {
					continue;
				}
				auto normal = cross * (1 / twiceArea), center = (a.position + b.position + c.position) / 3,
					 velocity = (a.velocity + b.velocity + c.velocity) / 3;
				for (auto& [fid, f] : forces.fields()) {
					if (f.kind == ForceField::Kind::Wind && (!f.target || f.target == id) && (f.targets & PHYSICS_COMMON::Cloth)
						&& (f.mask & s.layer)) {
						auto full = f.evaluate(center, velocity, 1, mTime);
						// Projected area vanishes for flow parallel to the cloth.
						float speed = full.magnitude();
						auto load = speed > 1e-10f ? full * (std::abs(full.dot(normal)) / speed * twiceArea / 6) : Vec3 {};
						for (auto i : triangle) {
							wind[i] += load;
						}
					}
				}
			}
		}
		for (std::size_t i = 0; i < s.particles.size(); ++i) {
			auto& p = s.particles[i];
			s.previous[i] = p.position;
			if (p.inverseMass == 0) {
				p.velocity = {};
				continue;
			}
			Vec3 force = wind[i];
			for (auto& [fid, f] : forces.fields()) {
				if ((f.target && f.target != id) || !(f.targets & (s.cloth ? PHYSICS_COMMON::Cloth : PHYSICS_COMMON::Soft)) || !(f.mask & s.layer)
					|| (s.cloth && f.kind == ForceField::Kind::Wind)) {
					continue;
				}
				force += f.evaluate(p.position, p.velocity, p.inverseMass, mTime);
			}
			p.velocity = (p.velocity + (settings.gravity + force * p.inverseMass) * dt) / (1 + s.damping * dt);
			p.position += p.velocity * dt;
		}
	}
	struct Ref {
		Particle* p;
		Id owner;
		std::size_t index;
		float radius;
		std::uint32_t layer, mask;
		bool fluid;
	};
	std::vector<Ref> all, liquid;
	for (auto& [id, s] : mSoft) {
		for (std::size_t i = 0; i < s.particles.size(); ++i) {
			all.push_back({ &s.particles[i], id, i, s.radius, s.layer, s.mask, false });
		}
	}
	for (auto& [id, f] : mFluids) {
		for (std::size_t i = 0; i < f.particles.size(); ++i) {
			auto& p = f.particles[i];
			f.previous[i] = p.position;
			auto force = forces.force(id, PHYSICS_COMMON::Fluid, f.layer, p.position, p.velocity, p.inverseMass, mTime);
			p.velocity += (settings.gravity + force * p.inverseMass) * dt;
			p.position += p.velocity * dt;
			Ref r { &p, id, i, f.radius, f.layer, f.mask, true };
			liquid.push_back(r);
			all.push_back(r);
		}
	}
	std::vector<float> lambdas(liquid.size());
	std::vector<Vec3> corrections(liquid.size());
	for (int iteration = 0; iteration < settings.particleIterations; ++iteration) {
		for (auto& [id, s] : mSoft) {
			for (std::size_t i = 0; i < s.links.size(); ++i) {
				auto c = s.links[i];
				c.compliance += s.compliance;
				PHYSICS_COMMON::solveDistance(s.particles, c, dt, s.linkLambda[i]);
			}
			for (std::size_t i = 0; i < s.tetrahedra.size(); ++i) {
				auto t = s.tetrahedra[i];
				auto a = s.particles[t[0]].position, b = s.particles[t[1]].position, c = s.particles[t[2]].position, d = s.particles[t[3]].position;
				std::array<Vec3, 4> g;
				g[1] = (c - a).cross(d - a) / 6;
				g[2] = (d - a).cross(b - a) / 6;
				g[3] = (b - a).cross(c - a) / 6;
				g[0] = -(g[1] + g[2] + g[3]);
				float den = 0;
				for (int j = 0; j < 4; ++j) {
					den += s.particles[t[j]].inverseMass * g[j].squareMagnitude();
				}
				float dl = PHYSICS_COMMON::multiplier(volume(s.particles, t) - s.restVolumes[i], den, s.volumeCompliance, dt, s.volumeLambda[i]);
				for (int j = 0; j < 4; ++j) {
					s.particles[t[j]].position += g[j] * (dl * s.particles[t[j]].inverseMass);
				}
			}
		}
		if (!liquid.empty()) {
			auto& f = mFluids.begin()->second;
			float h = f.smoothingRadius;
			SpatialGrid grid(*mPipeline.neighbors, h);
			for (std::size_t i = 0; i < liquid.size(); ++i) {
				grid.insert(i, liquid[i].p->position, liquid[i].p->position);
			}
			std::vector<std::vector<std::size_t>> neighbours(liquid.size());
			for (std::size_t i = 0; i < liquid.size(); ++i) {
				float density = 0, den = 0;
				Vec3 self;
				for (auto j : grid.query(liquid[i].p->position, h)) {
					if (!(liquid[i].layer & liquid[j].mask) || !(liquid[j].layer & liquid[i].mask)) {
						continue;
					}
					auto r = liquid[i].p->position - liquid[j].p->position;
					if (r.squareMagnitude() >= h * h) {
						continue;
					}
					neighbours[i].push_back(j);
					density += f.particleMass * kernel(r, h);
					if (i != j) {
						auto g = gradient(r, h) * (f.particleMass / f.restDensity);
						den += g.squareMagnitude();
						self += g;
					}
				}
				den += self.squareMagnitude();
				lambdas[i] = -std::max(0.0f, density / f.restDensity - 1) / (den + f.relaxation);
			}
			for (std::size_t i = 0; i < liquid.size(); ++i) {
				Vec3 correction;
				for (auto j : neighbours[i]) {
					if (i != j) {
						correction += gradient(liquid[i].p->position - liquid[j].p->position, h)
							* ((lambdas[i] + lambdas[j]) * f.particleMass / f.restDensity);
					}
				}
				corrections[i] = correction;
			}
			for (std::size_t i = 0; i < liquid.size(); ++i) {
				liquid[i].p->position += corrections[i];
			}
		}
		float cellSize = 0.1f;
		for (auto r : all) {
			cellSize = std::max(cellSize, 2 * r.radius);
		}
		SpatialGrid grid(*mPipeline.neighbors, cellSize);
		for (std::size_t i = 0; i < all.size(); ++i) {
			grid.insert(i, all[i].p->position - Vec3(all[i].radius), all[i].p->position + Vec3(all[i].radius));
		}
		for (std::size_t i = 0; i < all.size(); ++i) {
			for (auto j : grid.query(all[i].p->position, all[i].radius)) {
				if (j <= i) {
					continue;
				}
				auto& a = all[i];
				auto& b = all[j];
				if (a.fluid && b.fluid) {
					continue;
				}
				if (!(a.layer & b.mask) || !(b.layer & a.mask)) {
					continue;
				}
				if (a.owner == b.owner) {
					auto& s = mSoft.at(a.owner);
					if (!s.selfCollision) {
						continue;
					}
					if (std::any_of(s.links.begin(), s.links.end(),
							[&](auto l) { return (l.a == a.index && l.b == b.index) || (l.b == a.index && l.a == b.index); })) {
						continue;
					}
				}
				auto delta = b.p->position - a.p->position;
				float distance = delta.magnitude(), den = a.p->inverseMass + b.p->inverseMass, r = a.radius + b.radius;
				if (distance >= r || den < 1e-12f) {
					continue;
				}
				auto n = distance > 1e-8f ? delta * (1 / distance) : Vec3(1, 0, 0);
				auto c = n * ((r - distance) / den);
				a.p->position -= c * a.p->inverseMass;
				b.p->position += c * b.p->inverseMass;
			}
		}
		struct Face {
			Id owner;
			Triangle indices;
			Particle *a, *b, *c;
			float radius;
			std::uint32_t layer, mask;
		};
		std::vector<Face> faces;
		SpatialGrid surface(*mPipeline.neighbors, cellSize);
		for (auto& [id, s] : mSoft) {
			if (s.impermeable || s.selfCollision) {
				for (auto t : s.triangles) {
					auto &a = s.particles[t[0]], &b = s.particles[t[1]], &c = s.particles[t[2]];
					surface.insert(faces.size(), minimum(minimum(a.position, b.position), c.position) - Vec3(s.radius),
						maximum(maximum(a.position, b.position), c.position) + Vec3(s.radius));
					faces.push_back({ id, t, &a, &b, &c, s.radius, s.layer, s.mask });
				}
			}
		}
		for (auto r : all) {
			for (auto j : surface.query(r.p->position, r.radius)) {
				auto& f = faces[j];
				auto& s = mSoft.at(f.owner);
				if (!(r.layer & f.mask) || !(f.layer & r.mask)) {
					continue;
				}
				if (r.fluid && !s.impermeable) {
					continue;
				}
				if (r.owner == f.owner) {
					if (!s.selfCollision || std::find(f.indices.begin(), f.indices.end(), r.index) != f.indices.end()) {
						continue;
					}
					bool adjacent = false;
					for (auto l : s.links) {
						if (l.a == r.index || l.b == r.index) {
							auto other = l.a == r.index ? l.b : l.a;
							if (std::find(f.indices.begin(), f.indices.end(), other) != f.indices.end()) {
								adjacent = true;
								break;
							}
						}
					}
					if (adjacent) {
						continue;
					}
				}
				auto bary = weights(r.p->position, f.a->position, f.b->position, f.c->position);
				auto nearest = f.a->position * bary.x + f.b->position * bary.y + f.c->position * bary.z;
				auto delta = r.p->position - nearest;
				float length = delta.magnitude(), radius = r.radius + f.radius;
				if (length >= radius) {
					continue;
				}
				auto n = length > 1e-8f ? delta * (1 / length) : (f.b->position - f.a->position).cross(f.c->position - f.a->position);
				if (n.squareMagnitude() < 1e-14f) {
					continue;
				}
				n = n * (1 / n.magnitude());
				float den
					= r.p->inverseMass + f.a->inverseMass * bary.x * bary.x + f.b->inverseMass * bary.y * bary.y + f.c->inverseMass * bary.z * bary.z;
				if (den < 1e-12f) {
					continue;
				}
				auto correction = n * ((radius - length) / den);
				r.p->position += correction * r.p->inverseMass;
				f.a->position -= correction * (f.a->inverseMass * bary.x);
				f.b->position -= correction * (f.b->inverseMass * bary.y);
				f.c->position -= correction * (f.c->inverseMass * bary.z);
			}
		}
		// Edge/edge contacts cover crossings with no vertex near a triangle.
		struct Edge {
			Id owner;
			std::size_t a, b;
			float radius;
			std::uint32_t layer, mask;
		};
		std::vector<Edge> edges;
		SpatialGrid edgeGrid(*mPipeline.neighbors, cellSize);
		for (auto& [id, s] : mSoft) {
			std::set<std::pair<std::size_t, std::size_t>> unique;
			for (auto t : s.triangles) {
				for (int k = 0; k < 3; ++k) {
					unique.emplace(std::min(t[k], t[(k + 1) % 3]), std::max(t[k], t[(k + 1) % 3]));
				}
			}
			for (auto [a, b] : unique) {
				auto p = s.particles[a].position, q = s.particles[b].position;
				edgeGrid.insert(edges.size(), minimum(p, q) - Vec3(s.radius), maximum(p, q) + Vec3(s.radius));
				edges.push_back({ id, a, b, s.radius, s.layer, s.mask });
			}
		}
		for (std::size_t i = 0; i < edges.size(); ++i) {
			auto& e = edges[i];
			auto& sa = mSoft.at(e.owner);
			auto &a = sa.particles[e.a], &b = sa.particles[e.b];
			auto mid = (a.position + b.position) * 0.5f;
			float reach = (a.position - b.position).magnitude() * 0.5f + e.radius;
			for (auto j : edgeGrid.query(mid, reach)) {
				if (j <= i) {
					continue;
				}
				auto& f = edges[j];
				auto& sb = mSoft.at(f.owner);
				if (!(e.layer & f.mask) || !(f.layer & e.mask)) {
					continue;
				}
				if (e.owner == f.owner) {
					if (!sa.selfCollision || e.a == f.a || e.a == f.b || e.b == f.a || e.b == f.b) {
						continue;
					}
					bool adjacent = std::any_of(sa.links.begin(), sa.links.end(), [&](auto l) {
						return ((l.a == e.a || l.a == e.b) && (l.b == f.a || l.b == f.b))
							|| ((l.b == e.a || l.b == e.b) && (l.a == f.a || l.a == f.b));
					});
					if (adjacent) {
						continue;
					}
				}
				auto &c = sb.particles[f.a], &d = sb.particles[f.b];
				auto u = b.position - a.position, v = d.position - c.position, r = a.position - c.position;
				float aa = u.dot(u), bb = u.dot(v), cc = v.dot(v), dd = u.dot(r), ee = v.dot(r);
				if (aa < 1e-14f || cc < 1e-14f) {
					continue;
				}
				float den = aa * cc - bb * bb, s = den > 1e-14f ? std::clamp((bb * ee - cc * dd) / den, 0.0f, 1.0f) : 0;
				float t = (bb * s + ee) / cc;
				if (t < 0) {
					t = 0;
					s = std::clamp(-dd / aa, 0.0f, 1.0f);
				}
				else if (t > 1) {
					t = 1;
					s = std::clamp((bb - dd) / aa, 0.0f, 1.0f);
				}
				auto delta = a.position + u * s - c.position - v * t;
				float length = delta.magnitude(), radius = e.radius + f.radius;
				if (length >= radius) {
					continue;
				}
				auto n = length > 1e-8f ? delta * (1 / length) : u.cross(v);
				if (n.squareMagnitude() < 1e-14f) {
					continue;
				}
				n = n * (1 / n.magnitude());
				auto oldDelta = sa.previous[e.a] * (1 - s) + sa.previous[e.b] * s - sb.previous[f.a] * (1 - t) - sb.previous[f.b] * t;
				if (length < 1e-8f && oldDelta.dot(n) < 0) {
					n = -n;
				}
				float wa = 1 - s, wb = s, wc = 1 - t, wd = t;
				float effective = a.inverseMass * wa * wa + b.inverseMass * wb * wb + c.inverseMass * wc * wc + d.inverseMass * wd * wd;
				if (effective < 1e-12f) {
					continue;
				}
				auto correction = n * ((radius - length) / effective);
				a.position += correction * (wa * a.inverseMass);
				b.position += correction * (wb * b.inverseMass);
				c.position -= correction * (wc * c.inverseMass);
				d.position -= correction * (wd * d.inverseMass);
			}
		}
		solveAttachments(dt);
		auto solids = mPipeline.neighbors->create(settings.cellSize);
		if (!solids) {
			throw std::invalid_argument("Neighbor factory returned null");
		}
		for (const auto& [id, b] : mBodies) {
			solids->upsert(id, COLLISION::bounds(geometry(b)));
		}
		for (auto& [id, s] : mSoft) {
			for (auto& p : s.particles) {
				projectParticle(p, s.radius, s.friction, s.layer, s.mask, dt, *solids);
			}
		}
		for (auto& [id, f] : mFluids) {
			for (auto& p : f.particles) {
				projectParticle(p, f.radius, 0.2f, f.layer, f.mask, dt, *solids);
			}
		}
	}
	for (auto& [id, s] : mSoft) {
		for (std::size_t i = 0; i < s.particles.size(); ++i) {
			auto& p = s.particles[i];
			p.velocity = p.inverseMass > 0 ? (p.position - s.previous[i]) / dt : Vec3 {};
		}
	}
	for (auto& [id, f] : mFluids) {
		for (std::size_t i = 0; i < f.particles.size(); ++i) {
			f.particles[i].velocity = (f.particles[i].position - f.previous[i]) / dt;
		}
	}
	if (!liquid.empty()) {
		auto& f = mFluids.begin()->second;
		SpatialGrid grid(*mPipeline.neighbors, f.smoothingRadius);
		std::vector<Vec3> velocities(liquid.size());
		for (std::size_t i = 0; i < liquid.size(); ++i) {
			grid.insert(i, liquid[i].p->position, liquid[i].p->position);
		}
		for (std::size_t i = 0; i < liquid.size(); ++i) {
			Vec3 sum;
			float weight = 0;
			for (auto j : grid.query(liquid[i].p->position, f.smoothingRadius)) {
				if (i != j && (liquid[i].layer & liquid[j].mask) && (liquid[j].layer & liquid[i].mask)) {
					float w = kernel(liquid[i].p->position - liquid[j].p->position, f.smoothingRadius);
					sum += (liquid[j].p->velocity - liquid[i].p->velocity) * w;
					weight += w;
				}
			}
			velocities[i] = liquid[i].p->velocity + (weight > 1e-8f ? sum * (f.viscosity / weight) : Vec3 {});
		}
		for (std::size_t i = 0; i < liquid.size(); ++i) {
			liquid[i].p->velocity = velocities[i];
		}
	}
}
}
