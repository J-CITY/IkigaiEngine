#include "weather.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ENVIRONMENT {
float WeatherRuntime::random() {
	mRandom ^= mRandom << 13;
	mRandom ^= mRandom >> 17;
	mRandom ^= mRandom << 5;
	return float(mRandom >> 8) / 16777216.0f;
}
void WeatherRuntime::update(const Settings& settings, const GpuState& state, MATH::Vector3f anchor, float dt, const Trace& trace) {
	dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, .25f) : 0;
	if (mRandom == 0) {
		mRandom = settings.Seed ? settings.Seed : 1;
	}
	mQueries = 0;
	mImpacts.clear();
	const auto cameraDelta = anchor - mAnchor;
	if (cameraDelta.dot(cameraDelta) > settings.PrecipitationRadius * settings.PrecipitationRadius * 4) {
		mParticles.clear();
	}
	mAnchor = anchor;
	std::array<unsigned, 2> counts = {};
	for (auto& particle : mParticles) {
		particle.Lifetime -= dt;
		particle.Position += particle.Velocity * dt;
		particle.Remaining -= dt;
		if (particle.HasHit && particle.Remaining <= 0 && particle.Kind < 2) {
			if (particle.Kind == 0) {
				mImpacts.push_back(particle.Hit);
			}
			particle.Lifetime = 0;
		}
		const auto offset = particle.Position - anchor;
		if (std::abs(offset.x) > settings.PrecipitationRadius * 1.5f || std::abs(offset.z) > settings.PrecipitationRadius * 1.5f) {
			particle.Lifetime = 0;
		}
	}
	std::erase_if(mParticles, [](const WeatherParticle& particle) { return particle.Lifetime <= 0; });
	for (const auto& particle : mParticles) {
		if (particle.Kind < 2) {
			++counts[particle.Kind];
		}
	}
	const std::array<unsigned, 2> targets
		= { unsigned(settings.MaxParticles * state.WeatherState.x), unsigned(settings.MaxParticles * state.WeatherState.y) };
	// Remove excess particles gradually as weather fades, preserving particles of the other type.
	for (auto& particle : mParticles) {
		if (particle.Kind < 2 && counts[particle.Kind] > targets[particle.Kind]) {
			particle.Lifetime = 0;
			--counts[particle.Kind];
		}
	}
	std::erase_if(mParticles, [](const WeatherParticle& particle) { return particle.Lifetime <= 0; });
	const unsigned budget = settings.CollisionBudget;
	const size_t existing = mParticles.size();
	for (unsigned kind = 0; kind < 2; ++kind) {
		unsigned attempts = 0;
		while (counts[kind] < targets[kind] && mParticles.size() < settings.MaxParticles && attempts++ < 256) {
			if (trace && mQueries >= budget / 2 + (existing == 0 ? budget / 2 : 0)) {
				break;
			}
			const float speed = kind == 0 ? settings.RainSpeed : settings.SnowSpeed;
			WeatherParticle particle;
			particle.Kind = kind;
			particle.Velocity = { state.Wind.x * (kind == 0 ? .5f : .9f), -speed, state.Wind.y * (kind == 0 ? .5f : .9f) };
			if (kind == 1) {
				particle.Velocity.x += (random() - .5f) * .7f;
				particle.Velocity.z += (random() - .5f) * .7f;
			}
			const MATH::Vector3f top = anchor
				+ MATH::Vector3f { (random() * 2 - 1) * settings.PrecipitationRadius, settings.PrecipitationHeight,
					  (random() * 2 - 1) * settings.PrecipitationRadius };
			const float age = random() * settings.PrecipitationHeight / speed;
			particle.Position = top + particle.Velocity * age;
			particle.Lifetime = (settings.PrecipitationHeight + 4) / speed - age;
			particle.Size = kind == 0 ? .025f + random() * .02f : .06f + random() * .08f;
			if (trace) {
				++mQueries;
				if (const auto hit = trace(top, particle.Velocity * ((settings.PrecipitationHeight + 4) / speed))) {
					const float hitTime = (top.y - hit->Position.y) / speed;
					if (hitTime <= age) {
						continue; // roof intercepted this trajectory before the spawn position
					}
					particle.HasHit = true;
					particle.Hit = *hit;
					particle.Remaining = hitTime - age;
				}
			}
			mParticles.push_back(particle);
			++counts[kind];
		}
	}
	// Refresh trajectories with a bounded round-robin query budget for moving shelters.
	for (size_t checked = 0; trace && checked < existing && mQueries < budget && !mParticles.empty(); ++checked) {
		mQueryCursor %= mParticles.size();
		auto& particle = mParticles[mQueryCursor++];
		if (particle.Kind > 1) {
			continue;
		}
		++mQueries;
		particle.HasHit = false;
		const float speed = -particle.Velocity.y;
		const float aboveTime = std::max((anchor.y + settings.PrecipitationHeight - particle.Position.y) / speed, 0.0f);
		const auto top = particle.Position - particle.Velocity * aboveTime;
		if (const auto hit = trace(top, particle.Velocity * (aboveTime + particle.Lifetime))) {
			const float time = (particle.Position.y - hit->Position.y) / speed;
			if (time < 0) {
				particle.Lifetime = 0;
			}
			else {
				particle.HasHit = true;
				particle.Hit = *hit;
				particle.Remaining = time;
			}
		}
	}
	for (const auto& hit : mImpacts) {
		if (mParticles.size() >= settings.MaxParticles + 128) {
			break;
		}
		WeatherParticle splash;
		splash.Position = hit.Position + hit.Normal * .02f;
		splash.Velocity = hit.Normal * .15f;
		splash.Kind = 2;
		splash.Lifetime = .25f;
		splash.Size = .12f;
		mParticles.push_back(splash);
	}
}
const std::vector<WeatherParticle>& WeatherRuntime::particles() const {
	return mParticles;
}
const std::vector<WeatherHit>& WeatherRuntime::impacts() const {
	return mImpacts;
}
unsigned WeatherRuntime::queries() const {
	return mQueries;
}
}
