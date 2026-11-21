#pragma once
#include "mathModule/curves.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace IKIGAI::PARTICLES {
using Value = std::array<float, 4>;
using Parameters = std::map<std::string, std::vector<float>>;
enum class Domain { ParticleNormalized, ParticleAge, EmitterNormalized, EmitterAge };
enum class SourceMode { Constant, Uniform, Curve, CurveRange, Parameter };
struct Source {
	SourceMode mode = SourceMode::Constant;
	Domain domain = Domain::ParticleNormalized;
	int channels = 1;
	Value min { }, max { };
	std::array<MATH::CURVES::FloatCurve, 4> lower, upper;
	bool independent = false;
	std::string parameter;
	uint64_t channel = 0;
};
enum class ModuleType {
	Shape,
	Initialize,
	RadialVelocity,
	DirectionalVelocity,
	Acceleration,
	Drag,
	VelocityOverLife,
	IntegrateMotion,
	SizeOverLife,
	ColorOverLife,
	AlphaOverLife,
	RotationOverLife,
	AngularVelocity,
	OrientationOverLife,
	AngularVelocity3D,
	PositionOffsetOverLife,
	PositionOverLife,
	Flipbook
};
enum class ShapeType { Point, Segment, Rectangle, Circle, Ring, Box, Sphere, Cone, Cylinder };
struct Shape {
	ShapeType type = ShapeType::Point;
	Value offset { }, rotation { }, size { 1, 1, 1, 0 };
	float radius = 1, innerRadius = 0, height = 1;
	bool surface = false;
};
struct Module {
	ModuleType type = ModuleType::Initialize;
	uint64_t id = 0;
	Shape shape;
	Source value, lifetime, velocity, size, color, rotation, orientation;
	bool multiplyBase = true;
};
struct Burst {
	double time = 0, interval = 0;
	uint32_t count = 0;
};
enum class EventType { Birth, Death };
struct EventRoute {
	uint64_t id = 0;
	std::string emitterId;
	EventType on = EventType::Death;
	size_t emitter = 0;
	uint32_t count = 1;
	float probability = 1, inheritVelocity = 0;
	bool inheritColor = false;
};
struct EventLimits {
	uint32_t maxEventsPerStep = 4096, maxChildParticlesPerStep = 16384, maxDepth = 8;
};
struct ParticleEvent {
	EventType type = EventType::Birth;
	size_t emitter = 0;
	uint64_t particle = 0, randomKey = 0;
	uint32_t depth = 0;
	double time = 0;
	MATH::Vector3f position, velocity;
	Value color { 1, 1, 1, 1 };
};
struct EventStatistics {
	uint64_t droppedEvents = 0, droppedSpawns = 0, depthLimited = 0;
};
enum class Blend { Alpha, Additive, Premultiplied };
enum class Alignment { Camera, AxisLocked, Velocity };
struct Renderer {
	std::string texture, atlas, piece, material, mesh;
	uint32_t meshIndex = 0;
	bool screen = true, depthTest = true;
	int layer = 0, order = 0, columns = 1, rows = 1;
	MATH::Vector2f pivot { .5f, .5f };
	Blend blend = Blend::Alpha;
	Alignment alignment = Alignment::Camera;
};
struct EmitterDefinition {
	std::string id;
	uint32_t capacity = 1024, loops = 1;
	double duration = 1, delay = 0;
	bool local = false, inheritScale = true, kinematic = false, eventsOnly = false;
	float inheritVelocity = 0;
	Source rate;
	std::vector<Burst> bursts;
	std::vector<EventRoute> events;
	std::vector<Module> spawn, update;
	Renderer renderer;
};
struct Definition {
	int dimension = 2;
	EventLimits eventLimits;
	Parameters parameters;
	std::vector<EmitterDefinition> emitters;
};
// Strict, independent of ECS, VFS and GPU. All failures include a JSON path.
std::shared_ptr<const Definition> parse(const nlohmann::json& json, const std::string& path = { });
uint64_t hash(const std::string& text);
float random01(uint64_t key);

template <int D> struct Particle {
	uint64_t id = 0, randomKey = 0;
	uint32_t eventDepth = 0;
	std::array<float, D> position { }, previous { }, spawnPosition { }, velocity { }, offset { };
	Value baseSize { 1, 1, 0, 0 }, size { 1, 1, 0, 0 }, baseColor { 1, 1, 1, 1 }, color { 1, 1, 1, 1 };
	float age = 0, lifetime = 1, rotation = 0, baseRotation = 0, frame = 0, stepBirth = 0, planeZ = 0;
	MATH::QuaternionF orientation, baseOrientation;
	std::array<MATH::Vector3f, 3> birthBasis { MATH::Vector3f { 1, 0, 0 }, MATH::Vector3f { 0, 1, 0 }, MATH::Vector3f { 0, 0, 1 } };
};
// Column basis includes particle scale and the emitter transform in the selected simulation space.
MATH::Matrix4f meshTransform(const Particle<3>& particle, const EmitterDefinition& emitter, MATH::Matrix4f owner);
template <int D> struct EmitterState {
	std::vector<Particle<D>> particles;
	uint64_t serial = 0, dropped = 0;
	double fraction = 0;
};
enum class State { Playing, Paused, Draining, Complete };
template <int D> class Runtime {
public:
	explicit Runtime(std::shared_ptr<const Definition> definition, uint64_t seed = 1);
	void play();
	void pause();
	void resume();
	void stop(bool clear = false);
	void restart(uint64_t seed);
	void setParameter(const std::string& name, const std::vector<float>& value);
	void emit(size_t emitter, uint32_t count);
	void update(double dt, const MATH::Matrix4f& transform = MATH::Matrix4f::Identity);
	bool isAlive() const;
	bool isComplete() const {
		return mState == State::Complete;
	}
	State state() const {
		return mState;
	}
	const auto& emitters() const {
		return mEmitters;
	}
	const auto& definition() const {
		return *mDefinition;
	}
	// Snapshots from the last update/emit call, in chronological order; world coordinates.
	const auto& events() const {
		return mEvents;
	}
	const auto& eventStatistics() const {
		return mEventStatistics;
	}
	uint64_t droppedSteps() const {
		return mDroppedSteps;
	}
	static constexpr double Step = 1.0 / 60.0;

private:
	std::shared_ptr<const Definition> mDefinition;
	Parameters mParameters;
	std::vector<EmitterState<D>> mEmitters;
	std::vector<ParticleEvent> mEvents;
	EventStatistics mEventStatistics;
	struct SpawnContext {
		ParticleEvent event;
		float inheritVelocity = 0;
		bool inheritColor = false;
	};
	State mState = State::Playing;
	State mResume = State::Playing;
	uint64_t mSeed = 1, mDroppedSteps = 0;
	double mTime = 0, mAccumulator = 0;
	bool mInitial = true, mHaveTransform = false;
	MATH::Matrix4f mTransform = MATH::Matrix4f::Identity;
	MATH::Vector3f mEmitterVelocity { }, mLastOrigin { };
	void step(float dt, size_t manualEmitter = SIZE_MAX, uint32_t manualCount = 0);
	size_t spawn(size_t emitter, float birth, float emitterAge, size_t liveCount, const SpawnContext* context = nullptr, uint64_t lineage = 0,
		size_t reuseSlot = SIZE_MAX);
	float emitterAge(size_t emitter, double time) const;
	void updateParticle(Particle<D>& particle, const EmitterDefinition& emitter, float dt, float emitterAge);
	Value sample(const Source& source, uint64_t key, float age, float lifetime, float emitterAge, float duration) const;
};
extern template class Runtime<2>;
extern template class Runtime<3>;
}
