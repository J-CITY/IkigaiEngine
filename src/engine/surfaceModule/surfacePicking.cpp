#include "surfacePicking.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace IKIGAI::SURFACE {
namespace {
	bool slab(float origin, float direction, float minimum, float maximum, float& enter, float& leave) {
		if (std::abs(direction) < 1e-10f) {
			return origin >= minimum && origin <= maximum;
		}
		const float a = (minimum - origin) / direction;
		const float b = (maximum - origin) / direction;
		enter = std::max(enter, std::min(a, b));
		leave = std::min(leave, std::max(a, b));
		return enter <= leave;
	}
	std::optional<float> triangleHit(
		const MATH::Vector3f& origin, const MATH::Vector3f& direction, const MATH::Vector3f& a, const MATH::Vector3f& b, const MATH::Vector3f& c) {
		const auto edge = b - a;
		const auto other = c - a;
		const auto p = MATH::Vector3f::Cross(direction, other);
		const float determinant = edge.dot(p);
		const float magnitude = std::sqrt(edge.dot(edge) * other.dot(other) * direction.dot(direction));
		if (std::abs(determinant) <= magnitude * 1e-7f) {
			return {};
		}
		const auto offset = origin - a;
		const float u = offset.dot(p) / determinant;
		const auto q = MATH::Vector3f::Cross(offset, edge);
		const float v = direction.dot(q) / determinant;
		if (u < -.00001f || v < -.00001f || u + v > 1.00001f) {
			return {};
		}
		return other.dot(q) / determinant;
	}
}
std::optional<SurfaceHit> raycastSurface(
	const Settings& settings, const MATH::Vector3f& origin, const MATH::Vector3f& direction, float maximumParameter) {
	if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) || !std::isfinite(direction.x) || !std::isfinite(direction.y)
		|| !std::isfinite(direction.z) || !std::isfinite(maximumParameter) || maximumParameter <= 0 || direction.dot(direction) < 1e-12f) {
		return {};
	}
	const float halfX = settings.TilesX * settings.TileSize * .5f;
	const float halfZ = settings.TilesZ * settings.TileSize * .5f;
	const auto [low, high] = heightBounds(settings);
	float enter = 0;
	float leave = maximumParameter;
	if (!slab(origin.x, direction.x, -halfX, halfX, enter, leave) || !slab(origin.z, direction.z, -halfZ, halfZ, enter, leave)
		|| !slab(origin.y, direction.y, low - .001f, high + .001f, enter, leave)) {
		return {};
	}
	const float step = settings.TileSize / settings.Resolution;
	const int width = int(settings.TilesX * settings.Resolution);
	const int depth = int(settings.TilesZ * settings.Resolution);
	const auto start = origin + direction * enter;
	int x = std::clamp(int(std::floor((start.x + halfX) / step)), 0, width - 1);
	int z = std::clamp(int(std::floor((start.z + halfZ) / step)), 0, depth - 1);
	const int dx = direction.x > 0 ? 1 : -1;
	const int dz = direction.z > 0 ? 1 : -1;
	const float infinity = std::numeric_limits<float>::infinity();
	float nextX = std::abs(direction.x) < 1e-10f ? infinity : (-halfX + (x + (dx > 0 ? 1 : 0)) * step - origin.x) / direction.x;
	float nextZ = std::abs(direction.z) < 1e-10f ? infinity : (-halfZ + (z + (dz > 0 ? 1 : 0)) * step - origin.z) / direction.z;
	const float deltaX = std::abs(direction.x) < 1e-10f ? infinity : step / std::abs(direction.x);
	const float deltaZ = std::abs(direction.z) < 1e-10f ? infinity : step / std::abs(direction.z);
	for (int visit = 0; visit < width + depth + 4 && x >= 0 && x < width && z >= 0 && z < depth; ++visit) {
		const float px = -halfX + x * step;
		const float pz = -halfZ + z * step;
		const MATH::Vector3f a { px, height(settings, px, pz), pz };
		const MATH::Vector3f b { px, height(settings, px, pz + step), pz + step };
		const MATH::Vector3f c { px + step, height(settings, px + step, pz), pz };
		const MATH::Vector3f d { px + step, height(settings, px + step, pz + step), pz + step };
		std::optional<float> best;
		for (const auto hit : { triangleHit(origin, direction, a, b, c), triangleHit(origin, direction, d, c, b) }) {
			if (hit && *hit >= enter - .0001f && *hit <= leave + .0001f && *hit >= 0 && (!best || *hit < *best)) {
				best = hit;
			}
		}
		if (best) {
			auto position = origin + direction * *best;
			position.x = std::clamp(position.x, -halfX, halfX);
			position.z = std::clamp(position.z, -halfZ, halfZ);
			const auto sample = project(settings, position.x, position.z);
			return SurfaceHit { sample.Position, sample.Normal, *best };
		}
		const float next = std::min(nextX, nextZ);
		if (next > leave || !std::isfinite(next)) {
			break;
		}
		if (nextX <= nextZ) {
			x += dx;
			nextX += deltaX;
		}
		if (nextZ <= next) {
			z += dz;
			nextZ += deltaZ;
		}
		enter = next;
	}
	return {};
}
}
