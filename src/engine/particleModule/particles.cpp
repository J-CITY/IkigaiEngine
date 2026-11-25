#include "particles.h"
#include <algorithm>
#include <limits>
#include <queue>

namespace IKIGAI::PARTICLES {
uint64_t hash(const std::string& text) {
	uint64_t h = 14695981039346656037ull;
	for (unsigned char c : text) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}
namespace {
	uint64_t mixedKey(uint64_t key) {
		key += 0x9e3779b97f4a7c15ull;
		key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ull;
		key = (key ^ (key >> 27)) * 0x94d049bb133111ebull;
		return key ^ (key >> 31);
	}
}
float random01(uint64_t key) {
	return float(mixedKey(key) >> 40) * (1.f / 16777216.f);
}
namespace {
	std::array<MATH::Vector3f, 3> basis(MATH::Matrix4f matrix, bool inheritScale) {
		std::array<MATH::Vector3f, 3> result;
		for (int c = 0; c < 3; ++c) {
			result[c] = { matrix(0, c), matrix(1, c), matrix(2, c) };
			if (!inheritScale) {
				const auto length = MATH::Vector3f::Length(result[c]);
				if (length > 1e-8f)
					result[c] = result[c] / length;
			}
		}
		return result;
	}
	Value rotate(Value p, Value degrees) {
		for (int axis = 0; axis < 3; ++axis) {
			const float a = degrees[axis] * MATH::PI / 180, c = std::cos(a), s = std::sin(a);
			const int x = (axis + 1) % 3, y = (axis + 2) % 3;
			const float v = p[x];
			p[x] = c * v - s * p[y];
			p[y] = s * v + c * p[y];
		}
		return p;
	}
	Value point(const Shape& s, uint64_t key) {
		unsigned serial = 0;
		auto u = [&]() { return random01(key + (++serial) * 0x9e3779b97f4a7c15ull); };
		Value p { };
		const float a = 2 * MATH::PI * u();
		switch (s.type) {
		case ShapeType::Point:
			break;
		case ShapeType::Segment: {
			const float t = s.surface ? (u() < .5f ? -.5f : .5f) : u() - .5f;
			for (int c = 0; c < 3; ++c)
				p[c] = s.size[c] * t;
			break;
		}
		case ShapeType::Rectangle:
			p = { (u() - .5f) * s.size[0], (u() - .5f) * s.size[1], 0, 0 };
			if (s.surface) {
				if (u() * (s.size[0] + s.size[1]) < s.size[1])
					p[0] = (u() < .5f ? -.5f : .5f) * s.size[0];
				else
					p[1] = (u() < .5f ? -.5f : .5f) * s.size[1];
			}
			break;
		case ShapeType::Circle:
		case ShapeType::Ring: {
			float r = s.radius;
			if (s.surface && s.type == ShapeType::Ring)
				r = u() * (s.radius + s.innerRadius) < s.innerRadius ? s.innerRadius : s.radius;
			else if (!s.surface) {
				const float inner = s.type == ShapeType::Ring ? s.innerRadius : 0;
				r = std::sqrt(inner * inner + u() * (s.radius * s.radius - inner * inner));
			}
			p = { r * std::cos(a), r * std::sin(a), 0, 0 };
			break;
		}
		case ShapeType::Box: {
			p = { (u() - .5f) * s.size[0], (u() - .5f) * s.size[1], (u() - .5f) * s.size[2], 0 };
			if (s.surface) {
				const float x = s.size[1] * s.size[2], y = s.size[0] * s.size[2], z = s.size[0] * s.size[1], v = u() * (x + y + z);
				const int c = v < x ? 0 : v < x + y ? 1 : 2;
				p[c] = (u() < .5f ? -.5f : .5f) * s.size[c];
			}
			break;
		}
		case ShapeType::Sphere: {
			const float z = u() * 2 - 1, r = s.radius * (s.surface ? 1 : std::cbrt(u())), xy = std::sqrt(std::max(0.f, 1 - z * z));
			p = { r * xy * std::cos(a), r * z, r * xy * std::sin(a), 0 };
			break;
		}
		case ShapeType::Cylinder: {
			float r = s.radius, y = (u() - .5f) * s.height;
			if (!s.surface)
				r *= std::sqrt(u());
			else if (u() * (s.height + s.radius) < s.radius) {
				y = (u() < .5f ? -.5f : .5f) * s.height;
				r *= std::sqrt(u());
			}
			p = { r * std::cos(a), y, r * std::sin(a), 0 };
			break;
		}
		case ShapeType::Cone: {
			float t = std::cbrt(u()), r = s.radius * t * std::sqrt(u()), y = s.height * (1 - t);
			if (s.surface) {
				const float slant = std::sqrt(s.radius * s.radius + s.height * s.height);
				if (u() * (s.radius + slant) < s.radius) {
					y = 0;
					r = s.radius * std::sqrt(u());
				}
				else {
					t = std::sqrt(u());
					r = s.radius * t;
					y = s.height * (1 - t);
				}
			}
			p = { r * std::cos(a), y, r * std::sin(a), 0 };
			break;
		}
		}
		p = rotate(p, s.rotation);
		for (int c = 0; c < 3; ++c)
			p[c] += s.offset[c];
		return p;
	}
	template <int D> std::array<float, D> transformed(const std::array<float, D>& p, MATH::Matrix4f matrix, bool position, bool inheritScale) {
		if (!inheritScale)
			for (int c = 0; c < 3; ++c) {
				float l = 0;
				for (int r = 0; r < 3; ++r)
					l += matrix(r, c) * matrix(r, c);
				l = std::sqrt(l);
				if (l > 1e-8f)
					for (int r = 0; r < 3; ++r)
						matrix(r, c) /= l;
			}
		const auto v = matrix * MATH::Vector4f(p[0], p[1], D == 3 ? p[D - 1] : 0, position ? 1.f : 0.f);
		std::array<float, D> out { };
		out[0] = v.x;
		out[1] = v.y;
		if constexpr (D == 3)
			out[2] = v.z;
		return out;
	}
}

MATH::Matrix4f meshTransform(const Particle<3>& p, const EmitterDefinition& e, MATH::Matrix4f owner) {
	const auto axes = e.local ? basis(owner, e.inheritScale) : p.birthBasis;
	auto transformVector = [&](MATH::Vector3f v) { return axes[0] * v.x + axes[1] * v.y + axes[2] * v.z; };
	auto result = MATH::Matrix4f::Identity;
	for (int c = 0; c < 3; ++c) {
		MATH::Vector3f axis { };
		axis[c] = std::max(0.f, p.size[c]);
		const auto v = transformVector(p.orientation * axis);
		for (int row = 0; row < 3; ++row)
			result(row, c) = v[row];
	}
	MATH::Vector3f position { p.position[0] + p.offset[0], p.position[1] + p.offset[1], p.position[2] + p.offset[2] };
	if (e.local)
		position = transformVector(position) + MATH::Vector3f { owner(0, 3), owner(1, 3), owner(2, 3) };
	for (int row = 0; row < 3; ++row)
		result(row, 3) = position[row];
	return result;
}

template <int D>
Runtime<D>::Runtime(std::shared_ptr<const Definition> definition, uint64_t seed)
	: mDefinition(std::move(definition))
	, mSeed(seed) {
	if (!mDefinition || mDefinition->dimension != D)
		throw std::invalid_argument("Particle dimension mismatch");
	mParameters = mDefinition->parameters;
	restart(seed);
}
template <int D> void Runtime<D>::play() {
	if (mState == State::Complete || mState == State::Draining)
		restart(mSeed);
	else
		resume();
}
template <int D> void Runtime<D>::pause() {
	if (mState == State::Playing || mState == State::Draining) {
		mResume = mState;
		mState = State::Paused;
	}
}
template <int D> void Runtime<D>::resume() {
	if (mState == State::Paused) {
		mState = mResume;
		mHaveTransform = false;
	}
}
template <int D> void Runtime<D>::stop(bool clear) {
	mEvents.clear();
	mState = State::Draining;
	if (clear) {
		for (auto& e : mEmitters)
			e.particles.clear();
		mState = State::Complete;
	}
	mAccumulator = 0;
}
template <int D> bool Runtime<D>::isAlive() const {
	for (const auto& e : mEmitters)
		if (!e.particles.empty())
			return true;
	return false;
}
template <int D> void Runtime<D>::restart(uint64_t seed) {
	mSeed = seed;
	mTime = mAccumulator = 0;
	mInitial = true;
	mHaveTransform = false;
	mDroppedSteps = 0;
	mEvents.clear();
	mEventStatistics = { };
	mState = State::Playing;
	mResume = State::Playing;
	mEmitters.clear();
	mEmitters.resize(mDefinition->emitters.size());
	for (size_t i = 0; i < mEmitters.size(); ++i)
		mEmitters[i].particles.reserve(mDefinition->emitters[i].capacity);
}
template <int D> void Runtime<D>::setEventSpawnsEnabled(bool enabled) {
	mEventSpawnsEnabled = enabled;
}

template <int D> bool Runtime<D>::eventSpawnsEnabled() const {
	return mEventSpawnsEnabled;
}

template <int D> void Runtime<D>::setParameter(const std::string& name, const std::vector<float>& value) {
	auto it = mParameters.find(name);
	if (it == mParameters.end() || it->second.size() != value.size())
		throw std::invalid_argument("Invalid particle parameter: " + name);
	for (float v : value)
		if (!std::isfinite(v))
			throw std::invalid_argument("Non-finite particle parameter: " + name);
	it->second = value;
}
template <int D> void Runtime<D>::emit(size_t emitter, uint32_t count) {
	if (emitter >= mEmitters.size() || count > 65536)
		throw std::invalid_argument("Invalid manual particle emission");
	mEvents.clear();
	if (count && mState == State::Complete)
		mState = State::Draining;
	step(0, emitter, count);
}
template <int D> float Runtime<D>::emitterAge(size_t index, double time) const {
	const auto& e = mDefinition->emitters[index];
	const double age = std::max(0.0, time - e.delay);
	return e.loops && age >= e.duration * e.loops ? float(e.duration) : float(std::fmod(age, e.duration));
}
template <int D> Value Runtime<D>::sample(const Source& s, uint64_t key, float age, float lifetime, float emitterAge, float duration) const {
	if (s.mode == SourceMode::Parameter) {
		Value out { };
		const auto& p = mParameters.at(s.parameter);
		std::copy(p.begin(), p.end(), out.begin());
		return out;
	}
	Value out { };
	float time = age;
	if (s.domain == Domain::ParticleNormalized)
		time = lifetime > 0 ? std::clamp(age / lifetime, 0.f, 1.f) : 0;
	else if (s.domain == Domain::EmitterAge)
		time = emitterAge;
	else if (s.domain == Domain::EmitterNormalized)
		time = std::clamp(emitterAge / duration, 0.f, 1.f);
	for (int c = 0; c < s.channels; ++c) {
		const float r = random01(key ^ s.channel ^ (s.independent ? uint64_t(c + 1) * 0xd1b54a32d192ed03ull : 0));
		if (s.mode == SourceMode::Constant)
			out[c] = s.min[c];
		else if (s.mode == SourceMode::Uniform)
			out[c] = s.min[c] + (s.max[c] - s.min[c]) * r;
		else {
			const float a = s.lower[c].evaluate(time);
			out[c] = s.mode == SourceMode::Curve ? a : a + (s.upper[c].evaluate(time) - a) * r;
		}
	}
	return out;
}
template <int D> void Runtime<D>::update(double dt, const MATH::Matrix4f& transform) {
	if (!std::isfinite(dt) || dt < 0)
		throw std::invalid_argument("Invalid particle dt");
	mEvents.clear();
	if (mState == State::Paused || mState == State::Complete)
		return;
	const auto p = transform * MATH::Vector4f(0, 0, 0, 1);
	const MATH::Vector3f origin { p.x, p.y, p.z };
	mEmitterVelocity = { };
	if (mHaveTransform && dt > 0)
		mEmitterVelocity = (origin - mLastOrigin) / float(dt);
	mLastOrigin = origin;
	mHaveTransform = true;
	mTransform = transform;
	if (mInitial) {
		step(0);
		mInitial = false;
	}
	const double budget = 8 * Step;
	const double total = mAccumulator + dt;
	if (total > budget)
		mDroppedSteps += uint64_t(std::floor((total - budget) / Step));
	// Drop only whole steps and keep the same clock for particles and emission.
	mAccumulator = total > budget ? budget + std::fmod(total - budget, Step) : total;
	int steps = 0;
	while (mAccumulator + 1e-10 >= Step && steps < 8) {
		step(float(Step));
		mAccumulator -= Step;
		++steps;
	}
	mAccumulator = std::max(0.0, mAccumulator);
}
template <int D>
size_t Runtime<D>::spawn(
	size_t index, float birth, float emitterAge, size_t liveCount, const SpawnContext* context, uint64_t lineage, size_t reuseSlot) {
	auto& state = mEmitters[index];
	const auto& e = mDefinition->emitters[index];
	const auto serial = state.serial++;
	if (liveCount >= e.capacity) {
		++state.dropped;
		return SIZE_MAX;
	}
	MATH::Vector3f anchor { }, inheritedVelocity { };
	if (context) {
		anchor = context->event.position;
		inheritedVelocity = context->event.velocity * context->inheritVelocity;
		if (e.local) {
			auto transform = mTransform;
			const auto axes = basis(transform, e.inheritScale);
			if (std::abs(MATH::Vector3f::Dot(axes[0], MATH::Vector3f::Cross(axes[1], axes[2]))) < 1e-12f) {
				++state.dropped;
				return SIZE_MAX;
			}
			for (int c = 0; c < 3; ++c)
				for (int r = 0; r < 3; ++r)
					transform(r, c) = axes[c][r];
			const auto inverse = MATH::Matrix4f::Inverse(transform);
			const auto point = inverse * MATH::Vector4f(anchor.x, anchor.y, anchor.z, 1);
			const auto speed = inverse * MATH::Vector4f(inheritedVelocity.x, inheritedVelocity.y, inheritedVelocity.z, 0);
			anchor = { point.x, point.y, point.z };
			inheritedVelocity = { speed.x, speed.y, speed.z };
		}
		else {
			anchor = anchor - MATH::Vector3f { mTransform(0, 3), mTransform(1, 3), mTransform(2, 3) };
		}
	}
	Particle<D> p;
	p.id = serial;
	p.randomKey = mSeed ^ hash(e.id) ^ (serial * 0x9e3779b97f4a7c15ull) ^ (context ? mixedKey(lineage) : 0);
	if (context)
		p.eventDepth = context->event.depth + 1;
	p.stepBirth = birth;
	Value local { };
	for (const auto& m : e.spawn)
		if (m.type == ModuleType::Shape)
			local = point(m.shape, p.randomKey ^ m.id);
	for (int c = 0; c < D; ++c)
		p.position[c] = local[c];
	for (const auto& m : e.spawn) {
		const auto key = p.randomKey ^ m.id;
		auto get = [&](const Source& s) { return sample(s, key, 0, 1, emitterAge, float(e.duration)); };
		if (m.type == ModuleType::Initialize) {
			p.lifetime = get(m.lifetime)[0];
			if (!(p.lifetime > 0) || !std::isfinite(p.lifetime))
				throw std::invalid_argument("Invalid sampled lifetime in " + e.id);
			const auto v = get(m.velocity);
			for (int c = 0; c < D; ++c)
				p.velocity[c] = v[c];
			p.size = p.baseSize = get(m.size);
			p.color = p.baseColor = get(m.color);
			p.rotation = p.baseRotation = get(m.rotation)[0];
			if (!e.renderer.mesh.empty()) {
				const auto angles = get(m.orientation);
				p.orientation = p.baseOrientation = MATH::QuaternionF(MATH::Vector3f { angles[0], angles[1], angles[2] });
			}
		}
		else if (m.type == ModuleType::DirectionalVelocity) {
			const float angle = get(m.rotation)[0] * MATH::PI / 180, speed = get(m.value)[0];
			p.velocity[0] += std::cos(angle) * speed;
			p.velocity[1] += std::sin(angle) * speed;
		}
		else if (m.type == ModuleType::RadialVelocity) {
			Value origin { };
			for (const auto& shape : e.spawn)
				if (shape.type == ModuleType::Shape)
					origin = shape.shape.offset;
			float length = 0;
			for (int c = 0; c < D; ++c)
				length += (local[c] - origin[c]) * (local[c] - origin[c]);
			length = std::sqrt(length);
			const float speed = get(m.value)[0];
			for (int c = 0; c < D; ++c)
				p.velocity[c] += (length > 1e-8f ? (local[c] - origin[c]) / length : (c == 1 ? 1.f : 0.f)) * speed;
		}
	}
	if (!e.local) {
		if (!e.renderer.mesh.empty())
			p.birthBasis = basis(mTransform, e.inheritScale);
		if constexpr (D == 2) {
			auto matrix = mTransform;
			const auto origin = matrix * MATH::Vector4f(p.position[0], p.position[1], 0, 1);
			p.planeZ = origin.z;
			p.rotation += std::atan2(matrix(1, 0), matrix(0, 0)) * 180 / MATH::PI;
			p.baseRotation = p.rotation;
		}
		p.position = transformed<D>(p.position, mTransform, true, e.inheritScale);
		p.velocity = transformed<D>(p.velocity, mTransform, false, e.inheritScale);
		if (e.inheritScale && e.renderer.mesh.empty()) {
			auto matrix = mTransform;
			for (int c = 0; c < 2; ++c) {
				float s = 0;
				for (int r = 0; r < 3; ++r)
					s += matrix(r, c) * matrix(r, c);
				p.baseSize[c] *= std::sqrt(s);
			}
			p.size = p.baseSize;
		}
		p.velocity[0] += mEmitterVelocity.x * e.inheritVelocity;
		p.velocity[1] += mEmitterVelocity.y * e.inheritVelocity;
		if constexpr (D == 3)
			p.velocity[2] += mEmitterVelocity.z * e.inheritVelocity;
	}
	if (context) {
		for (int c = 0; c < D; ++c) {
			p.position[c] += anchor[c];
			p.velocity[c] += inheritedVelocity[c];
		}
		if constexpr (D == 2)
			p.planeZ += anchor.z;
		if (context->inheritColor)
			for (int c = 0; c < 4; ++c)
				p.baseColor[c] *= context->event.color[c];
		p.color = p.baseColor;
	}
	p.spawnPosition = p.previous = p.position;
	if (reuseSlot != SIZE_MAX) {
		state.particles[reuseSlot] = p;
		return reuseSlot;
	}
	state.particles.push_back(p);
	return state.particles.size() - 1;
}
template <int D> void Runtime<D>::updateParticle(Particle<D>& p, const EmitterDefinition& e, float dt, float emitterAge) {
	p.previous = p.position;
	p.age += dt;
	p.size = p.baseSize;
	p.color = p.baseColor;
	p.offset = { };
	p.frame = 0;
	for (const auto& m : e.update) {
		const auto v = m.type == ModuleType::IntegrateMotion ? Value { }
															 : sample(m.value, p.randomKey ^ m.id, p.age, p.lifetime, emitterAge, float(e.duration));
		switch (m.type) {
		case ModuleType::Acceleration:
			for (int c = 0; c < D; ++c)
				p.velocity[c] += v[c] * dt;
			break;
		case ModuleType::Drag: {
			const float factor = std::exp(-std::max(0.f, v[0]) * dt);
			for (auto& c : p.velocity)
				c *= factor;
			break;
		}
		case ModuleType::VelocityOverLife:
			for (int c = 0; c < D; ++c)
				p.velocity[c] = v[c];
			break;
		case ModuleType::IntegrateMotion:
			for (int c = 0; c < D; ++c)
				p.position[c] += p.velocity[c] * dt;
			break;
		case ModuleType::SizeOverLife:
			for (int c = 0; c < m.value.channels; ++c)
				p.size[c] = std::max(0.f, v[c] * (m.multiplyBase ? p.baseSize[c] : 1));
			break;
		case ModuleType::ColorOverLife:
			for (int c = 0; c < 4; ++c)
				p.color[c] = v[c] * (m.multiplyBase ? p.baseColor[c] : 1);
			break;
		case ModuleType::AlphaOverLife:
			p.color[3] = v[0] * (m.multiplyBase ? p.baseColor[3] : 1);
			break;
		case ModuleType::RotationOverLife:
			p.rotation = v[0] + (m.multiplyBase ? p.baseRotation : 0);
			break;
		case ModuleType::AngularVelocity:
			p.rotation += v[0] * dt;
			break;
		case ModuleType::OrientationOverLife: {
			const MATH::QuaternionF rotation(MATH::Vector3f { v[0], v[1], v[2] });
			p.orientation = MATH::QuaternionF::Normalize(m.multiplyBase ? p.baseOrientation * rotation : rotation);
			break;
		}
		case ModuleType::AngularVelocity3D: {
			// Integrate a local angular velocity vector with an axis-angle delta.
			const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			if (speed > 1e-8f) {
				const float angle = speed * dt * MATH::PI / 360, s = std::sin(angle) / speed;
				p.orientation = MATH::QuaternionF::Normalize(p.orientation * MATH::QuaternionF(v[0] * s, v[1] * s, v[2] * s, std::cos(angle)));
			}
			break;
		}
		case ModuleType::PositionOffsetOverLife:
			for (int c = 0; c < D; ++c)
				p.offset[c] = v[c];
			break;
		case ModuleType::PositionOverLife:
			for (int c = 0; c < D; ++c)
				p.position[c] = p.spawnPosition[c] + v[c];
			break;
		case ModuleType::Flipbook:
			p.frame = v[0];
			break;
		default:
			break;
		}
	}
	if (e.kinematic && dt > 0)
		for (int c = 0; c < D; ++c)
			p.velocity[c] = (p.position[c] - p.previous[c]) / dt;
	p.color[3] = std::clamp(p.color[3], 0.f, 1.f);
}
template <int D> void Runtime<D>::step(float dt, size_t manualEmitter, uint32_t manualCount) {
	const double old = mTime, end = mTime + dt;
	const auto& limits = mDefinition->eventLimits;
	struct Birth {
		float offset, age;
	};
	enum class Kind { Death, Root, Child };
	struct Work {
		float offset;
		Kind kind;
		size_t emitter, slot;
		uint64_t key;
		std::shared_ptr<const SpawnContext> context;
	};
	auto later = [](const Work& a, const Work& b) {
		if (a.offset != b.offset)
			return a.offset > b.offset;
		if (a.kind != b.kind)
			return a.kind > b.kind;
		if (a.key != b.key)
			return a.key > b.key;
		return a.slot > b.slot;
	};
	std::priority_queue<Work, std::vector<Work>, decltype(later)> pending(later);
	std::vector<std::vector<Birth>> roots(mEmitters.size());
	std::vector<size_t> live(mEmitters.size());
	std::vector<std::vector<size_t>> freeSlots(mEmitters.size());
	auto queueDeath = [&](size_t index, size_t slot, float offset) {
		const auto& p = mEmitters[index].particles[slot];
		const float remaining = std::max(0.f, p.lifetime - p.age);
		if (remaining <= dt - offset + 1e-7f)
			pending.push({ std::min(dt, offset + remaining), Kind::Death, index, slot, p.randomKey, { } });
	};
	for (size_t index = 0; index < mEmitters.size(); ++index) {
		auto& state = mEmitters[index];
		const auto& e = mDefinition->emitters[index];
		live[index] = state.particles.size();
		for (size_t slot = 0; slot < state.particles.size(); ++slot) {
			state.particles[slot].stepBirth = 0;
			queueDeath(index, slot, 0);
		}
		auto& events = roots[index];
		auto add = [&](double absolute, double age, uint32_t count) {
			const size_t take = std::min<size_t>(count, 65536 - events.size());
			for (size_t n = 0; n < take; ++n)
				events.push_back({ std::clamp(float(absolute - old), 0.f, dt), float(age) });
			state.dropped += count - take;
			state.serial += count - take;
		};
		if (index == manualEmitter)
			add(old, emitterAge(index, old), manualCount);
		if (manualEmitter == SIZE_MAX && mState == State::Playing && !e.eventsOnly && end >= e.delay) {
			const uint64_t first = uint64_t(std::max(0.0, std::floor((old - e.delay) / e.duration)));
			const uint64_t last = uint64_t(std::max(0.0, std::floor((end - e.delay) / e.duration)));
			for (uint64_t cycle = first; cycle <= last && (!e.loops || cycle < e.loops); ++cycle) {
				const double start = e.delay + cycle * e.duration, a = std::max(old, start), b = std::min(end, start + e.duration);
				if (b < a)
					continue;
				for (const auto& burst : e.bursts) {
					const double span = burst.interval;
					uint64_t k = span > 0 ? uint64_t(std::max(0.0, std::ceil((a - start - burst.time) / span - 1e-9))) : 0;
					for (;; ++k) {
						const double local = burst.time + k * span, absolute = start + local;
						if (local > e.duration + 1e-9 || absolute > b + 1e-9)
							break;
						if (absolute > old + 1e-9 || (mInitial && std::abs(absolute - old) < 1e-9))
							add(absolute, local, burst.count);
						if (span == 0)
							break;
					}
				}
				if (b > a) {
					const float rate
						= std::clamp(sample(e.rate, mSeed ^ hash(e.id), 0, 1, float((a + b) * .5 - start), float(e.duration))[0], 0.f, 1000000.f);
					const double amount = (b - a) * rate, previous = state.fraction;
					const auto births = uint32_t(std::floor(previous + amount + 1e-9));
					state.fraction = previous + amount - births;
					for (uint32_t k = 0; k < births; ++k) {
						const double time = a + (k + 1 - previous) / rate;
						add(time, time - start, 1);
					}
				}
			}
		}

		std::stable_sort(events.begin(), events.end(), [](auto a, auto b) { return a.offset < b.offset; });
		if (!events.empty())
			pending.push({ events[0].offset, Kind::Root, index, 0, hash(e.id), { } });
	}
	uint32_t eventCount = 0, childCount = 0;
	auto dispatch = [&](size_t index, const Particle<D>& p, EventType type, float offset) {
		if (eventCount >= limits.maxEventsPerStep) {
			++mEventStatistics.droppedEvents;
			return;
		}
		++eventCount;
		const auto& e = mDefinition->emitters[index];
		ParticleEvent event;
		event.type = type;
		event.emitter = index;
		event.particle = p.id;
		event.randomKey = p.randomKey;
		event.time = old + offset;
		event.depth = p.eventDepth;
		event.color = p.color;
		event.position = { p.position[0] + p.offset[0], p.position[1] + p.offset[1], p.planeZ };
		event.velocity = { p.velocity[0], p.velocity[1], 0 };
		if constexpr (D == 3) {
			event.position.z = p.position[2] + p.offset[2];
			event.velocity.z = p.velocity[2];
		}
		if (e.local) {
			const auto axes = basis(mTransform, e.inheritScale);
			auto transform = [&](const MATH::Vector3f& v) { return axes[0] * v.x + axes[1] * v.y + axes[2] * v.z; };
			event.position = transform(event.position) + MATH::Vector3f { mTransform(0, 3), mTransform(1, 3), mTransform(2, 3) };
			event.velocity = transform(event.velocity);
		}
		mEvents.push_back(event);
		if (!mEventSpawnsEnabled) {
			return;
		}
		for (const auto& route : e.events) {
			const uint64_t key = p.randomKey ^ route.id ^ hash(type == EventType::Birth ? "birth" : "death");
			if (route.on != type || random01(key) >= route.probability)
				continue;
			uint32_t take = 0;
			if (event.depth < limits.maxDepth) {
				take = std::min(route.count, limits.maxChildParticlesPerStep - childCount);
				childCount += take;
			}
			else {
				mEventStatistics.depthLimited += route.count;
			}
			const auto skipped = route.count - take;
			mEventStatistics.droppedSpawns += skipped;
			mEmitters[route.emitter].dropped += skipped;
			mEmitters[route.emitter].serial += skipped;
			if (!take)
				continue;
			auto context = std::make_shared<SpawnContext>();
			context->event = event;
			context->inheritVelocity = route.inheritVelocity;
			context->inheritColor = route.inheritColor;
			for (uint32_t n = 0; n < take; ++n)
				pending.push({ offset, Kind::Child, route.emitter, n, key ^ ((n + 1) * 0x9e3779b97f4a7c15ull), context });
		}
	};
	while (!pending.empty()) {
		const auto work = pending.top();
		pending.pop();
		const size_t index = work.emitter;
		auto& state = mEmitters[index];
		const auto& e = mDefinition->emitters[index];
		const float age = emitterAge(index, old + work.offset);
		if (work.kind == Kind::Death) {
			auto& p = state.particles[work.slot];
			updateParticle(p, e, std::max(0.f, work.offset - p.stepBirth), age);
			p.age = p.lifetime;
			--live[index];
			dispatch(index, p, EventType::Death, work.offset);
			freeSlots[index].push_back(work.slot);
			continue;
		}
		if (work.kind == Kind::Root && work.slot + 1 < roots[index].size()) {
			const auto next = work.slot + 1;
			pending.push({ roots[index][next].offset, Kind::Root, index, next, work.key, { } });
		}
		const auto reuse = freeSlots[index].empty() ? SIZE_MAX : freeSlots[index].back();
		const auto slot = spawn(index, work.offset, work.kind == Kind::Root ? roots[index][work.slot].age : age, live[index], work.context.get(),
			work.kind == Kind::Child ? work.key : 0, reuse);
		if (slot == SIZE_MAX) {
			if (work.kind == Kind::Child)
				++mEventStatistics.droppedSpawns;
			continue;
		}
		++live[index];
		if (reuse != SIZE_MAX)
			freeSlots[index].pop_back();
		auto& p = state.particles[slot];
		updateParticle(p, e, 0, age);
		dispatch(index, p, EventType::Birth, work.offset);
		queueDeath(index, slot, work.offset);
	}
	bool finished = true;
	for (size_t index = 0; index < mEmitters.size(); ++index) {
		auto& state = mEmitters[index];
		const auto& e = mDefinition->emitters[index];
		for (auto& p : state.particles)
			if (p.age < p.lifetime)
				updateParticle(p, e, std::max(0.f, dt - p.stepBirth), emitterAge(index, end));
		std::erase_if(state.particles, [](const auto& p) { return p.age >= p.lifetime; });
		if (!state.particles.empty() || (mState == State::Playing && !e.eventsOnly && (!e.loops || end < e.delay + e.duration * e.loops)))
			finished = false;
	}
	mTime = end;
	if (finished && (manualEmitter == SIZE_MAX || mState == State::Draining))
		mState = State::Complete;
}
template class Runtime<2>;
template class Runtime<3>;
}
