#pragma once
#include "environment.h"
#include <functional>
#include <optional>
#include <vector>

namespace IKIGAI::ENVIRONMENT {
struct WeatherHit {
	MATH::Vector3f Position = {};
	MATH::Vector3f Normal = { 0, 1, 0 };
};
struct WeatherParticle {
	MATH::Vector3f Position = {};
	MATH::Vector3f Velocity = {};
	float Lifetime = 0;
	float Remaining = 0;
	float Size = 0;
	unsigned Kind = 0; // rain, snow, splash
	bool HasHit = false;
	WeatherHit Hit = {};
};
class WeatherRuntime {
public:
	using Trace = std::function<std::optional<WeatherHit>(MATH::Vector3f, MATH::Vector3f)>;
	void update(const Settings& settings, const GpuState& state, MATH::Vector3f anchor, float dt, const Trace& trace);
	const std::vector<WeatherParticle>& particles() const;
	const std::vector<WeatherHit>& impacts() const;
	unsigned queries() const;

private:
	std::vector<WeatherParticle> mParticles = {};
	std::vector<WeatherHit> mImpacts = {};
	MATH::Vector3f mAnchor = {};
	uint32_t mRandom = 0;
	unsigned mQueries = 0;
	size_t mQueryCursor = 0;
	float random();
};
}
