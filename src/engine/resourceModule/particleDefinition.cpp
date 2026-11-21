#include "particleDefinition.h"
#include "particleModule/particles.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <set>
#include <type_traits>
#include <utility>

namespace IKIGAI::RESOURCES {
using namespace PARTICLES;
namespace {
	using J = nlohmann::json;
	[[noreturn]] void fail(const std::string& p, const std::string& message) {
		throw std::invalid_argument(p + ": " + message);
	}
	J& required(J& j, const char* key, const std::string& p) {
		if (!j.contains(key)) {
			fail(p + "/" + key, "required field missing");
		}
		return j.at(key);
	}
	void fields(const J& j, const std::string& p, std::initializer_list<const char*> allowed) {
		if (!j.is_object()) {
			fail(p, "expected object");
		}
		for (auto it = j.begin(); it != j.end(); ++it) {
			bool found = false;
			for (auto name : allowed) {
				if (it.key() == name) {
					found = true;
					break;
				}
			}
			if (!found) {
				fail(p + "/" + it.key(), "unknown field");
			}
		}
	}
	// Particle assets require numeric JSON tokens and report errors at the field path.
	template <class T> struct ParticleField {
		T fallback;
		bool required;

		template <class Field, class Context, class Next> void from(Context& context, Field& value, std::string_view key, Next&& next) const {
			const auto field = std::string(key);
			const auto it = context.adaptor.find(field);
			if (it == context.adaptor.end()) {
				if (required) {
					fail(field, "required field missing");
				}
				value = fallback;
				return;
			}
			if constexpr (std::is_same_v<T, float>) {
				if (!it->is_number()) {
					fail(field, "expected number");
				}
			}
			try {
				next.from(context, value, key);
			} catch (const J::exception& error) {
				fail(field, error.what());
			}
			if constexpr (std::is_same_v<T, float>) {
				if (!std::isfinite(value)) {
					fail(field, "expected finite number");
				}
			}
		}
	};

	template <class T> auto requiredField() {
		return ParticleField<T> { T {}, true };
	}

	template <class T> auto optionalField(T fallback) {
		return ParticleField<T> { std::move(fallback), false };
	}

	template <class Descriptor> Descriptor readDescriptor(J& json, const std::string& path) {
		try {
			return serde::deserialize<Descriptor>(json);
		} catch (const std::invalid_argument& error) {
			throw std::invalid_argument(path + "/" + error.what());
		}
	}

	struct EffectDescriptor {
		float version {};
		std::string dimension {};

		template <class Context> static void serde(Context& context, EffectDescriptor& value) {
			using Self = EffectDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::version, "Version", requiredField<float>())
				.field(&Self::dimension, "Dimension", requiredField<std::string>());
		}
	};

	struct EventLimitsDescriptor {
		float maxEventsPerStep {};
		float maxChildParticlesPerStep {};
		float maxDepth {};

		template <class Context> static void serde(Context& context, EventLimitsDescriptor& value) {
			using Self = EventLimitsDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::maxEventsPerStep, "MaxEventsPerStep", optionalField<float>(4096))
				.field(&Self::maxChildParticlesPerStep, "MaxChildParticlesPerStep", optionalField<float>(16384))
				.field(&Self::maxDepth, "MaxDepth", optionalField<float>(8));
		}
	};

	struct TypeDescriptor {
		std::string type {};

		template <class Context> static void serde(Context& context, TypeDescriptor& value) {
			using Self = TypeDescriptor;
			serde::serde_struct(context, value).field(&Self::type, "Type", requiredField<std::string>());
		}
	};

	struct EmitterDescriptor {
		std::string id {};
		float capacity {};
		std::string simulationSpace {};
		std::string scalePolicy {};
		float inheritVelocity {};

		template <class Context> static void serde(Context& context, EmitterDescriptor& value) {
			using Self = EmitterDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::id, "Id", requiredField<std::string>())
				.field(&Self::capacity, "Capacity", optionalField<float>(1024))
				.field(&Self::simulationSpace, "SimulationSpace", optionalField<std::string>("world"))
				.field(&Self::scalePolicy, "ScalePolicy", optionalField<std::string>("inherit"))
				.field(&Self::inheritVelocity, "InheritVelocity", optionalField<float>(0));
		}
	};

	struct TimingDescriptor {
		float duration {};
		float delay {};
		float loops {};

		template <class Context> static void serde(Context& context, TimingDescriptor& value) {
			using Self = TimingDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::duration, "Duration", optionalField<float>(1))
				.field(&Self::delay, "Delay", optionalField<float>(0))
				.field(&Self::loops, "Loops", optionalField<float>(1));
		}
	};

	struct EmissionDescriptor {
		std::string mode {};

		template <class Context> static void serde(Context& context, EmissionDescriptor& value) {
			using Self = EmissionDescriptor;
			serde::serde_struct(context, value).field(&Self::mode, "Mode", optionalField<std::string>("automatic"));
		}
	};

	struct BurstDescriptor {
		float time {};
		float count {};
		float interval {};

		template <class Context> static void serde(Context& context, BurstDescriptor& value) {
			using Self = BurstDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::time, "Time", optionalField<float>(0))
				.field(&Self::count, "Count", requiredField<float>())
				.field(&Self::interval, "Interval", optionalField<float>(0));
		}
	};

	struct CurveKeyDescriptor {
		float time {};
		std::string interpolation {};

		template <class Context> static void serde(Context& context, CurveKeyDescriptor& value) {
			using Self = CurveKeyDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::time, "Time", requiredField<float>())
				.field(&Self::interpolation, "Interpolation", optionalField<std::string>("linear"));
		}
	};

	struct SourceDescriptor {
		std::string mode {};
		std::string domain {};
		std::string randomChannels {};

		template <class Context> static void serde(Context& context, SourceDescriptor& value) {
			using Self = SourceDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::mode, "Mode", optionalField<std::string>("constant"))
				.field(&Self::domain, "Domain", optionalField<std::string>("particleNormalizedAge"))
				.field(&Self::randomChannels, "RandomChannels", optionalField<std::string>("linked"));
		}
	};

	struct ParameterSourceDescriptor {
		std::string parameter {};

		template <class Context> static void serde(Context& context, ParameterSourceDescriptor& value) {
			using Self = ParameterSourceDescriptor;
			serde::serde_struct(context, value).field(&Self::parameter, "Parameter", requiredField<std::string>());
		}
	};

	struct ModuleDescriptor {
		std::string id {};
		std::string type {};

		template <class Context> static void serde(Context& context, ModuleDescriptor& value) {
			using Self = ModuleDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::id, "Id", requiredField<std::string>())
				.field(&Self::type, "Type", requiredField<std::string>());
		}
	};

	struct ShapeDescriptor {
		std::string shape {};
		float radius {};
		float innerRadius {};
		float height {};
		std::string sampling {};

		template <class Context> static void serde(Context& context, ShapeDescriptor& value) {
			using Self = ShapeDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::shape, "Shape", requiredField<std::string>())
				.field(&Self::radius, "Radius", optionalField<float>(1))
				.field(&Self::innerRadius, "InnerRadius", optionalField<float>(0))
				.field(&Self::height, "Height", optionalField<float>(1))
				.field(&Self::sampling, "Sampling", optionalField<std::string>("volume"));
		}
	};

	struct OperationDescriptor {
		std::string operation {};

		template <class Context> static void serde(Context& context, OperationDescriptor& value) {
			using Self = OperationDescriptor;
			serde::serde_struct(context, value).field(&Self::operation, "Operation", optionalField<std::string>("multiplyBase"));
		}
	};

	struct RendererDescriptor {
		std::string texture {};
		std::string atlas {};
		std::string piece {};
		std::string material {};
		std::optional<std::string> renderSpace {};
		std::optional<bool> depthTest {};
		int layer {};
		int order {};
		float columns {};
		float rows {};
		std::string blend {};
		std::string alignment {};

		template <class Context> static void serde(Context& context, RendererDescriptor& value) {
			using Self = RendererDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::texture, "Texture", optionalField<std::string>({}))
				.field(&Self::atlas, "Atlas", optionalField<std::string>({}))
				.field(&Self::piece, "Piece", optionalField<std::string>({}))
				.field(&Self::material, "Material", optionalField<std::string>({}))
				.field(&Self::renderSpace, "RenderSpace", optionalField<std::optional<std::string>>({}))
				.field(&Self::depthTest, "DepthTest", optionalField<std::optional<bool>>({}))
				.field(&Self::layer, "Layer", optionalField<int>(0))
				.field(&Self::order, "Order", optionalField<int>(0))
				.field(&Self::columns, "Columns", optionalField<float>(1))
				.field(&Self::rows, "Rows", optionalField<float>(1))
				.field(&Self::blend, "Blend", optionalField<std::string>("alpha"))
				.field(&Self::alignment, "Alignment", optionalField<std::string>("camera"));
		}
	};

	struct MeshRendererDescriptor {
		std::string mesh {};
		float meshIndex {};

		template <class Context> static void serde(Context& context, MeshRendererDescriptor& value) {
			using Self = MeshRendererDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::mesh, "Mesh", requiredField<std::string>())
				.field(&Self::meshIndex, "MeshIndex", optionalField<float>(0));
		}
	};

	struct EventDescriptor {
		std::string id {};
		std::string on {};
		std::string emitter {};
		float count {};
		float probability {};
		float inheritVelocity {};
		bool inheritColor {};

		template <class Context> static void serde(Context& context, EventDescriptor& value) {
			using Self = EventDescriptor;
			serde::serde_struct(context, value)
				.field(&Self::id, "Id", requiredField<std::string>())
				.field(&Self::on, "On", requiredField<std::string>())
				.field(&Self::emitter, "Emitter", requiredField<std::string>())
				.field(&Self::count, "Count", optionalField<float>(1))
				.field(&Self::probability, "Probability", optionalField<float>(1))
				.field(&Self::inheritVelocity, "InheritVelocity", optionalField<float>(0))
				.field(&Self::inheritColor, "InheritColor", optionalField<bool>(false));
		}
	};

	float number(const J& j, const std::string& p) {
		if (!j.is_number()) {
			fail(p, "expected number");
		}
		const float v = j.get<float>();
		if (!std::isfinite(v)) {
			fail(p, "expected finite number");
		}
		return v;
	}
	uint32_t integer(double n, const std::string& p, uint32_t max) {
		if (n < 0 || n > max || std::floor(n) != n) {
			fail(p, "integer out of range");
		}
		return uint32_t(n);
	}
	uint32_t integer(const J& j, const std::string& p, uint32_t max) {
		return integer(number(j, p), p, max);
	}
	Value value(const J& j, int n, const std::string& p) {
		Value v {};
		if (n == 1) {
			v[0] = number(j, p);
			return v;
		}
		if (!j.is_array() || j.size() != size_t(n)) {
			fail(p, "expected " + std::to_string(n) + " channels");
		}
		for (int i = 0; i < n; ++i) {
			v[i] = number(j[i], p + "/" + std::to_string(i));
		}
		return v;
	}
	Source constant(Value v, int n) {
		Source s;
		s.channels = n;
		s.min = s.max = v;
		return s;
	}
	void curves(J& keys, int n, std::array<MATH::CURVES::FloatCurve, 4>& out, const std::string& p) {
		if (!keys.is_array() || keys.empty() || keys.size() > 4096) {
			fail(p + "/Keys", "expected 1..4096 keys");
		}
		for (size_t i = 0; i < keys.size(); ++i) {
			const auto kp = p + "/Keys/" + std::to_string(i);
			auto& k = keys[i];
			fields(k, kp, { "Time", "Value", "Interpolation", "OutControl", "InControl", "Easing" });
			const auto descriptor = readDescriptor<CurveKeyDescriptor>(k, kp);
			const auto v = value(required(k, "Value", kp), n, kp + "/Value");
			MATH::CURVES::FloatKey base;
			base.time = descriptor.time;
			const auto& mode = descriptor.interpolation;
			if (mode == "step") {
				base.interpolation = MATH::CURVES::SegmentInterpolation::Step;
			}
			else if (mode == "linear") {
				base.interpolation = MATH::CURVES::SegmentInterpolation::Linear;
			}
			else if (mode == "bezier") {
				base.interpolation = MATH::CURVES::SegmentInterpolation::Bezier;
			}
			else if (mode == "easing") {
				base.interpolation = MATH::CURVES::SegmentInterpolation::Easing;
				const auto easing = integer(required(k, "Easing", kp), kp + "/Easing", uint32_t(MATH::CURVES::InterpolationType::CUSTOM) - 1);
				base.easing = static_cast<MATH::CURVES::InterpolationType>(easing);
			}
			else {
				fail(kp + "/Interpolation", "unsupported interpolation");
			}
			if (k.contains("OutControl")) {
				const auto a = value(k["OutControl"], 2, kp + "/OutControl");
				base.outControl = { a[0], a[1] };
			}
			if (k.contains("InControl")) {
				const auto a = value(k["InControl"], 2, kp + "/InControl");
				base.inControl = { a[0], a[1] };
			}
			for (int c = 0; c < n; ++c) {
				auto key = base;
				key.value = v[c];
				out[c].keys.push_back(key);
			}
		}
		try {
			for (int c = 0; c < n; ++c) {
				out[c].validate();
			}
		} catch (const std::exception& e) {
			fail(p, e.what());
		}
	}
	Source source(J& j, int n, const std::string& p, const Parameters& params) {
		fields(j, p, { "Mode", "Value", "Min", "Max", "Domain", "RandomChannels", "Parameter", "Keys" });
		Source s;
		s.channels = n;
		s.channel = hash(p);
		const auto descriptor = readDescriptor<SourceDescriptor>(j, p);
		const auto& mode = descriptor.mode;
		const auto& domain = descriptor.domain;
		if (domain == "particleNormalizedAge") {
			s.domain = Domain::ParticleNormalized;
		}
		else if (domain == "particleAge") {
			s.domain = Domain::ParticleAge;
		}
		else if (domain == "emitterNormalizedAge") {
			s.domain = Domain::EmitterNormalized;
		}
		else if (domain == "emitterAge") {
			s.domain = Domain::EmitterAge;
		}
		else {
			fail(p + "/Domain", "unsupported domain");
		}
		const auto& channels = descriptor.randomChannels;
		if (channels != "linked" && channels != "independent") {
			fail(p + "/RandomChannels", "expected linked or independent");
		}
		s.independent = channels == "independent";
		if (mode == "constant") {
			s.min = s.max = value(required(j, "Value", p), n, p + "/Value");
		}
		else if (mode == "uniform") {
			s.mode = SourceMode::Uniform;
			s.min = value(required(j, "Min", p), n, p + "/Min");
			s.max = value(required(j, "Max", p), n, p + "/Max");
			for (int c = 0; c < n; ++c) {
				if (s.min[c] > s.max[c]) {
					fail(p, "Min exceeds Max");
				}
			}
		}
		else if (mode == "curve") {
			s.mode = SourceMode::Curve;
			curves(required(j, "Keys", p), n, s.lower, p);
		}
		else if (mode == "curveRange") {
			s.mode = SourceMode::CurveRange;
			auto& minimum = required(j, "Min", p);
			auto& maximum = required(j, "Max", p);
			fields(minimum, p + "/Min", { "Keys" });
			fields(maximum, p + "/Max", { "Keys" });
			curves(required(minimum, "Keys", p + "/Min"), n, s.lower, p + "/Min");
			curves(required(maximum, "Keys", p + "/Max"), n, s.upper, p + "/Max");
		}
		else if (mode == "parameter") {
			s.mode = SourceMode::Parameter;
			s.parameter = readDescriptor<ParameterSourceDescriptor>(j, p).parameter;
			if (!params.contains(s.parameter) || params.at(s.parameter).size() != size_t(n)) {
				fail(p + "/Parameter", "missing parameter or incompatible type");
			}
		}
		else {
			fail(p + "/Mode", "unsupported source");
		}
		if (s.mode == SourceMode::Curve || s.mode == SourceMode::CurveRange) {
			if (s.domain == Domain::ParticleNormalized || s.domain == Domain::EmitterNormalized) {
				for (int c = 0; c < n; ++c) {
					for (const auto* curve : { &s.lower[c], &s.upper[c] }) {
						for (const auto& key : curve->keys) {
							if (key.time < 0 || key.time > 1) {
								fail(p, "normalized curve keys must lie in [0,1]");
							}
						}
					}
				}
			}
		}
		return s;
	}
	Module module(J& j, int d, bool mesh, bool spawn, const std::string& p, const Parameters& params) {
		fields(j, p,
			{ "Id", "Type", "Shape", "Offset", "Rotation", "Size", "Radius", "InnerRadius", "Height", "Sampling", "Lifetime", "Velocity", "Color",
			"Speed", "Angle", "Value", "Operation", "Orientation" });
		Module m;
		const auto descriptor = readDescriptor<ModuleDescriptor>(j, p);
		const auto& id = descriptor.id;
		if (id.empty()) {
			fail(p + "/Id", "empty id");
		}
		m.id = hash(id);
		const auto& type = descriptor.type;
		if (j.contains("Orientation") && type != "Initialize") {
			fail(p + "/Orientation", "Orientation belongs to Initialize");
		}
		auto src
			= [&](const char* name, int n, Value def) { return j.contains(name) ? source(j[name], n, p + "/" + name, params) : constant(def, n); };
		if (type == "Shape") {
			m.type = ModuleType::Shape;
			auto& s = m.shape;
			const std::map<std::string, ShapeType> types { { "point", ShapeType::Point }, { "segment", ShapeType::Segment },
				{ "rectangle", ShapeType::Rectangle }, { "circle", ShapeType::Circle }, { "ring", ShapeType::Ring }, { "box", ShapeType::Box },
				{ "sphere", ShapeType::Sphere }, { "cone", ShapeType::Cone }, { "cylinder", ShapeType::Cylinder } };
			const auto shape = readDescriptor<ShapeDescriptor>(j, p);
			const auto& name = shape.shape;
			if (!types.contains(name)) {
				fail(p + "/Shape", "unsupported shape");
			}
			s.type = types.at(name);
			if ((d == 2 && s.type >= ShapeType::Box) || (d == 3 && s.type >= ShapeType::Rectangle && s.type <= ShapeType::Ring)) {
				fail(p + "/Shape", "shape incompatible with dimension");
			}
			if (j.contains("Offset")) {
				s.offset = value(j["Offset"], d, p + "/Offset");
			}
			if (j.contains("Size")) {
				s.size = value(j["Size"], d, p + "/Size");
			}
			if (j.contains("Rotation")) {
				if (d == 2) {
					s.rotation[2] = number(j["Rotation"], p + "/Rotation");
				}
				else {
					s.rotation = value(j["Rotation"], 3, p + "/Rotation");
				}
			}
			s.radius = shape.radius;
			s.innerRadius = shape.innerRadius;
			s.height = shape.height;
			for (int c = 0; c < d; ++c) {
				if (s.size[c] < 0) {
					fail(p + "/Size", "negative size");
				}
			}
			if (s.radius < 0 || s.innerRadius < 0 || s.innerRadius > s.radius || s.height < 0) {
				fail(p, "invalid shape dimensions");
			}
			const auto& sampling = shape.sampling;
			if (sampling != "volume" && sampling != "surface") {
				fail(p + "/Sampling", "expected volume or surface");
			}
			s.surface = sampling == "surface";
		}
		else if (type == "Initialize") {
			m.type = ModuleType::Initialize;
			if (!j.contains("Lifetime") || !j.contains("Size")) {
				fail(p, "Initialize requires Lifetime and Size");
			}
			m.lifetime = src("Lifetime", 1, { 1, 0, 0, 0 });
			m.velocity = src("Velocity", d, {});
			m.size = src("Size", mesh ? 3 : 2, { 1, 1, 1, 0 });
			m.color = src("Color", 4, { 1, 1, 1, 1 });
			m.rotation = src("Rotation", 1, {});
			m.orientation = src("Orientation", 3, {});
			if (mesh && j.contains("Rotation")) {
				fail(p + "/Rotation", "mesh3D uses Orientation (Euler XYZ degrees)");
			}
			if (!mesh && j.contains("Orientation")) {
				fail(p + "/Orientation", "Orientation requires mesh3D");
			}
			if ((m.lifetime.mode == SourceMode::Constant || m.lifetime.mode == SourceMode::Uniform) && m.lifetime.min[0] <= 0) {
				fail(p + "/Lifetime", "lifetime must be positive");
			}
		}
		else {
			const std::map<std::string, ModuleType> types { { "DirectionalVelocity", ModuleType::DirectionalVelocity },
				{ "RadialVelocity", ModuleType::RadialVelocity }, { "Acceleration", ModuleType::Acceleration }, { "Drag", ModuleType::Drag },
				{ "VelocityOverLife", ModuleType::VelocityOverLife }, { "IntegrateMotion", ModuleType::IntegrateMotion },
				{ "SizeOverLife", ModuleType::SizeOverLife }, { "ColorOverLife", ModuleType::ColorOverLife },
				{ "AlphaOverLife", ModuleType::AlphaOverLife }, { "RotationOverLife", ModuleType::RotationOverLife },
				{ "AngularVelocity", ModuleType::AngularVelocity }, { "OrientationOverLife", ModuleType::OrientationOverLife },
				{ "AngularVelocity3D", ModuleType::AngularVelocity3D }, { "PositionOffsetOverLife", ModuleType::PositionOffsetOverLife },
				{ "PositionOverLife", ModuleType::PositionOverLife }, { "Flipbook", ModuleType::Flipbook } };
			if (!types.contains(type)) {
				fail(p + "/Type", "unsupported module");
			}
			m.type = types.at(type);
			if ((m.type == ModuleType::OrientationOverLife || m.type == ModuleType::AngularVelocity3D) && !mesh) {
				fail(p + "/Type", "3D orientation modules require mesh3D");
			}
			if ((m.type == ModuleType::RotationOverLife || m.type == ModuleType::AngularVelocity) && mesh) {
				fail(p + "/Type", "mesh3D requires OrientationOverLife or AngularVelocity3D");
			}
			const int n = (m.type == ModuleType::Acceleration || m.type == ModuleType::VelocityOverLife
							  || m.type == ModuleType::PositionOffsetOverLife || m.type == ModuleType::PositionOverLife)
				? d
				: (m.type == ModuleType::OrientationOverLife || m.type == ModuleType::AngularVelocity3D) ? 3
				: m.type == ModuleType::SizeOverLife													 ? (mesh ? 3 : 2)
				: m.type == ModuleType::ColorOverLife													 ? 4
																										 : 1;
			if (m.type != ModuleType::IntegrateMotion) {
				const char* key = (m.type == ModuleType::RadialVelocity || m.type == ModuleType::DirectionalVelocity) ? "Speed" : "Value";
				if (!j.contains(key)) {
					fail(p + "/" + key, "missing source");
				}
				m.value = source(j[key], n, p + "/" + key, params);
			}
			if (m.type == ModuleType::DirectionalVelocity) {
				if (d != 2) {
					fail(p, "DirectionalVelocity is 2D; use Velocity or RadialVelocity in 3D");
				}
				m.rotation = src("Angle", 1, {});
			}
			const auto op = readDescriptor<OperationDescriptor>(j, p).operation;
			if (op != "multiplyBase" && op != "set") {
				fail(p + "/Operation", "expected set or multiplyBase");
			}
			m.multiplyBase = op == "multiplyBase";
		}
		if (spawn
			!= (m.type == ModuleType::Shape || m.type == ModuleType::Initialize || m.type == ModuleType::RadialVelocity
				|| m.type == ModuleType::DirectionalVelocity)) {
			fail(p, "module in wrong stage");
		}
		// Random streams are keyed by stable IDs and property names, not array indices.
		m.value.channel = hash("Value");
		m.lifetime.channel = hash("Lifetime");
		m.velocity.channel = hash("Velocity");
		m.size.channel = hash("Size");
		m.color.channel = hash("Color");
		m.rotation.channel = hash("Rotation");
		m.orientation.channel = hash("Orientation");
		return m;
	}
}

std::shared_ptr<const Definition> ReadParticleDefinition(std::string_view contents, const std::string& asset) {
	auto j = J::parse(contents);
	const std::string root = asset.empty() ? "particle" : asset;
	try {
		fields(j, root, { "Version", "Dimension", "Parameters", "Emitters", "EventLimits" });
		const auto descriptor = readDescriptor<EffectDescriptor>(j, root);
		if (integer(descriptor.version, root + "/Version", 100) != 1) {
			fail(root + "/Version", "unsupported version");
		}
		auto def = std::make_shared<Definition>();
		if (j.contains("EventLimits")) {
			auto& limits = j["EventLimits"];
			const auto lp = root + "/EventLimits";
			fields(limits, lp, { "MaxEventsPerStep", "MaxChildParticlesPerStep", "MaxDepth" });
			const auto config = readDescriptor<EventLimitsDescriptor>(limits, lp);
			def->eventLimits.maxEventsPerStep = integer(config.maxEventsPerStep, lp + "/MaxEventsPerStep", 65536);
			if (!def->eventLimits.maxEventsPerStep) {
				fail(lp + "/MaxEventsPerStep", "must be positive");
			}
			def->eventLimits.maxChildParticlesPerStep = integer(config.maxChildParticlesPerStep, lp + "/MaxChildParticlesPerStep", 65536);
			def->eventLimits.maxDepth = integer(config.maxDepth, lp + "/MaxDepth", 32);
		}
		const auto& dim = descriptor.dimension;
		if (dim != "2D" && dim != "3D") {
			fail(root + "/Dimension", "expected 2D or 3D");
		}
		def->dimension = dim == "2D" ? 2 : 3;
		if (j.contains("Parameters")) {
			if (!j["Parameters"].is_object()) {
				fail(root + "/Parameters", "expected object");
			}
			for (auto it = j["Parameters"].begin(); it != j["Parameters"].end(); ++it) {
				const auto p = root + "/Parameters/" + it.key();
				fields(it.value(), p, { "Type", "Default" });
				const auto t = readDescriptor<TypeDescriptor>(it.value(), p).type;
				int n = t == "float" ? 1 : t == "vec2" ? 2 : t == "vec3" ? 3 : (t == "vec4" || t == "color") ? 4 : 0;
				if (!n) {
					fail(p + "/Type", "unsupported parameter type");
				}
				const auto v = value(required(it.value(), "Default", p), n, p + "/Default");
				def->parameters[it.key()] = { v.begin(), v.begin() + n };
			}
		}
		auto& emitters = required(j, "Emitters", root);
		if (!emitters.is_array() || emitters.empty() || emitters.size() > 128) {
			fail(root + "/Emitters", "expected 1..128 emitters");
		}
		std::set<std::string> ids;
		uint64_t totalCapacity = 0;
		for (size_t i = 0; i < emitters.size(); ++i) {
			const auto p = root + "/Emitters/" + std::to_string(i);
			auto& e = emitters[i];
			fields(e, p,
				{ "Id", "SimulationSpace", "Capacity", "Timing", "Emission", "Spawn", "Update", "Renderer", "ScalePolicy", "InheritVelocity",
				"Events" });
			EmitterDefinition out;
			const auto emitter = readDescriptor<EmitterDescriptor>(e, p);
			out.id = emitter.id;
			if (out.id.empty() || !ids.insert(out.id).second) {
				fail(p + "/Id", "empty or duplicate id");
			}
			out.capacity = integer(emitter.capacity, p + "/Capacity", 1000000);
			totalCapacity += out.capacity;
			if (!out.capacity || totalCapacity > 1000000) {
				fail(p + "/Capacity", "effect capacity must be in 1..1000000");
			}
			const auto& space = emitter.simulationSpace;
			if (space != "world" && space != "local") {
				fail(p + "/SimulationSpace", "expected local or world");
			}
			out.local = space == "local";
			const auto& scale = emitter.scalePolicy;
			if (scale != "inherit" && scale != "ignore") {
				fail(p + "/ScalePolicy", "expected inherit or ignore");
			}
			out.inheritScale = scale == "inherit";
			out.inheritVelocity = emitter.inheritVelocity;
			auto timing = e.value("Timing", J::object());
			fields(timing, p + "/Timing", { "Duration", "Delay", "Loops" });
			const auto schedule = readDescriptor<TimingDescriptor>(timing, p + "/Timing");
			out.duration = schedule.duration;
			out.delay = schedule.delay;
			if (out.duration < .001 || out.delay < 0) {
				fail(p + "/Timing", "Duration must be >= .001 and Delay >= 0");
			}
			out.loops = integer(schedule.loops, p + "/Timing/Loops", 1000000);
			auto emptyEmission = J::object();
			auto& emission = e.contains("Emission") ? e["Emission"] : emptyEmission;
			fields(emission, p + "/Emission", { "Rate", "Bursts", "Mode" });
			const auto mode = readDescriptor<EmissionDescriptor>(emission, p + "/Emission").mode;
			if (mode != "automatic" && mode != "eventsOnly") {
				fail(p + "/Emission/Mode", "expected automatic or eventsOnly");
			}
			out.eventsOnly = mode == "eventsOnly";
			if (out.eventsOnly && (emission.contains("Rate") || emission.contains("Bursts"))) {
				fail(p + "/Emission", "eventsOnly does not schedule Rate or Bursts");
			}
			if (emission.contains("Rate")) {
				out.rate = source(emission["Rate"], 1, p + "/Emission/Rate", def->parameters);
			}
			if ((out.rate.mode == SourceMode::Curve || out.rate.mode == SourceMode::CurveRange) && out.rate.domain != Domain::EmitterAge
				&& out.rate.domain != Domain::EmitterNormalized) {
				fail(p + "/Emission/Rate/Domain", "rate curves require an emitter domain");
			}
			if (emission.contains("Bursts")) {
				if (!emission["Bursts"].is_array() || emission["Bursts"].size() > 1024) {
					fail(p + "/Emission/Bursts", "expected at most 1024 bursts");
				}
				for (size_t b = 0; b < emission["Bursts"].size(); ++b) {
					const auto bp = p + "/Emission/Bursts/" + std::to_string(b);
					auto& v = emission["Bursts"][b];
					fields(v, bp, { "Time", "Count", "Interval" });
					const auto descriptor = readDescriptor<BurstDescriptor>(v, bp);
					Burst burst;
					burst.time = descriptor.time;
					burst.count = integer(descriptor.count, bp + "/Count", 65536);
					burst.interval = descriptor.interval;
					if (burst.time < 0 || burst.time > out.duration || burst.interval < 0 || (burst.interval > 0 && burst.interval < .001)) {
						fail(bp, "invalid burst time/interval");
					}
					out.bursts.push_back(burst);
				}
			}
			std::set<uint64_t> modules;
			auto& r = required(e, "Renderer", p);
			const auto rp = p + "/Renderer";
			const auto rendererType = readDescriptor<TypeDescriptor>(r, rp).type;
			const bool mesh = rendererType == "mesh3D";
			if (mesh && def->dimension != 3) {
				fail(p + "/Renderer/Type", "mesh3D requires Dimension 3D");
			}
			int orientations = 0;
			int init = 0, shape = 0, solver = 0, path = 0;
			bool afterSolver = false, radial = false;
			for (bool spawning : { true, false }) {
				const auto key = spawning ? "Spawn" : "Update";
				if (!e.contains(key)) {
					continue;
				}
				auto& array = e[key];
				if (!array.is_array() || array.size() > 64) {
					fail(p + "/" + key, "expected at most 64 modules");
				}
				for (size_t k = 0; k < array.size(); ++k) {
					const auto mp = p + "/" + key + "/" + std::to_string(k);
					auto m = module(array[k], def->dimension, mesh, spawning, mp, def->parameters);
					if (m.type == ModuleType::OrientationOverLife || m.type == ModuleType::AngularVelocity3D) {
						if (++orientations > 1) {
							fail(mp, "use one orientation update mode per emitter");
						}
					}
					if (!modules.insert(m.id).second) {
						fail(mp + "/Id", "duplicate module id");
					}
					if (m.type == ModuleType::Initialize) {
						if (radial) {
							fail(mp, "Initialize must precede RadialVelocity");
						}
						++init;
					}
					if (m.type == ModuleType::Shape) {
						if (radial) {
							fail(mp, "Shape must precede RadialVelocity");
						}
						++shape;
					}
					if (m.type == ModuleType::RadialVelocity) {
						radial = true;
					}
					if (m.type == ModuleType::IntegrateMotion) {
						++solver;
						afterSolver = true;
					}
					if (m.type == ModuleType::PositionOverLife) {
						++path;
					}
					if ((m.type == ModuleType::Acceleration || m.type == ModuleType::Drag || m.type == ModuleType::VelocityOverLife) && afterSolver) {
						fail(mp, "motion inputs must precede IntegrateMotion");
					}
					(spawning ? out.spawn : out.update).push_back(std::move(m));
				}
			}
			if (init != 1 || shape > 1 || solver > 1 || path > 1 || solver + path != 1) {
				fail(p, "need one Initialize and one motion mode, at most one Shape");
			}
			out.kinematic = path == 1;
			if (out.kinematic) {
				for (const auto& m : out.update) {
					if (m.type == ModuleType::Acceleration || m.type == ModuleType::Drag || m.type == ModuleType::VelocityOverLife) {
						fail(p + "/Update", "kinematic path conflicts with motion forces");
					}
				}
			}
			fields(r, rp,
				{ "Type", "RenderSpace", "Texture", "Atlas", "Piece", "Material", "Blend", "Layer", "Order", "DepthTest", "Alignment", "Pivot",
				"Columns", "Rows", "Mesh", "MeshIndex" });
			if (!mesh && rendererType != (def->dimension == 2 ? "sprite2D" : "billboard3D")) {
				fail(rp + "/Type", "renderer incompatible with dimension");
			}
			auto& rd = out.renderer;
			if (mesh) {
				const auto geometry = readDescriptor<MeshRendererDescriptor>(r, rp);
				rd.mesh = geometry.mesh;
				if (rd.mesh.empty()) {
					fail(rp + "/Mesh", "mesh asset path is empty");
				}
				rd.meshIndex = integer(geometry.meshIndex, rp + "/MeshIndex", 65535);
				for (const auto* field : { "Alignment", "Pivot" }) {
					if (r.contains(field)) {
						fail(rp + "/" + field, "billboard-only field");
					}
				}
			}
			else if (r.contains("Mesh") || r.contains("MeshIndex")) {
				fail(rp, "Mesh and MeshIndex require mesh3D");
			}
			const auto appearance = readDescriptor<RendererDescriptor>(r, rp);
			rd.texture = appearance.texture;
			rd.atlas = appearance.atlas;
			rd.piece = appearance.piece;
			rd.material = appearance.material;
			if (!rd.texture.empty() && !rd.atlas.empty()) {
				fail(rp, "Texture and Atlas are mutually exclusive");
			}
			const auto rs = appearance.renderSpace.value_or(def->dimension == 2 ? "screen" : "world");
			if (rs != "screen" && rs != "world") {
				fail(rp + "/RenderSpace", "expected screen or world");
			}
			rd.screen = rs == "screen";
			if (def->dimension == 3 && rd.screen) {
				fail(rp + "/RenderSpace", "3D requires world space");
			}
			rd.depthTest = appearance.depthTest.value_or(!rd.screen);
			rd.layer = appearance.layer;
			rd.order = appearance.order;
			if (r.contains("Pivot")) {
				const auto v = value(r["Pivot"], 2, rp + "/Pivot");
				rd.pivot = { v[0], v[1] };
			}
			rd.columns = integer(appearance.columns, rp + "/Columns", 4096);
			rd.rows = integer(appearance.rows, rp + "/Rows", 4096);
			if (!rd.columns || !rd.rows) {
				fail(rp, "flipbook dimensions must be positive");
			}
			const auto blend = appearance.blend;
			if (blend == "alpha") {
				rd.blend = Blend::Alpha;
			}
			else if (blend == "additive") {
				rd.blend = Blend::Additive;
			}
			else if (blend == "premultiplied") {
				rd.blend = Blend::Premultiplied;
			}
			else {
				fail(rp + "/Blend", "unsupported blend");
			}
			const auto align = appearance.alignment;
			if (align == "camera") {
				rd.alignment = Alignment::Camera;
			}
			else if (align == "velocity") {
				rd.alignment = Alignment::Velocity;
			}
			else if (align == "axisLocked") {
				rd.alignment = Alignment::AxisLocked;
			}
			else {
				fail(rp + "/Alignment", "unsupported alignment");
			}
			if (e.contains("Events")) {
				auto& routes = e["Events"];
				if (!routes.is_array() || routes.size() > 64) {
					fail(p + "/Events", "expected at most 64 routes");
				}
				std::set<uint64_t> routeIds;
				for (size_t k = 0; k < routes.size(); ++k) {
					const auto ep = p + "/Events/" + std::to_string(k);
					auto& route = routes[k];
					fields(route, ep, { "Id", "On", "Emitter", "Count", "Probability", "InheritVelocity", "InheritColor" });
					EventRoute event;
					const auto descriptor = readDescriptor<EventDescriptor>(route, ep);
					const auto& id = descriptor.id;
					event.id = hash(id);
					if (id.empty() || !routeIds.insert(event.id).second) {
						fail(ep + "/Id", "empty or duplicate route id");
					}
					const auto& on = descriptor.on;
					if (on != "birth" && on != "death") {
						fail(ep + "/On", "expected birth or death");
					}
					event.on = on == "birth" ? EventType::Birth : EventType::Death;
					event.emitterId = descriptor.emitter;
					event.count = integer(descriptor.count, ep + "/Count", 4096);
					if (!event.count) {
						fail(ep + "/Count", "must be positive");
					}
					event.probability = descriptor.probability;
					if (event.probability < 0 || event.probability > 1) {
						fail(ep + "/Probability", "expected [0,1]");
					}
					event.inheritVelocity = descriptor.inheritVelocity;
					event.inheritColor = descriptor.inheritColor;
					out.events.push_back(std::move(event));
				}
			}
			def->emitters.push_back(std::move(out));
		}
		for (size_t i = 0; i < def->emitters.size(); ++i) {
			for (size_t k = 0; k < def->emitters[i].events.size(); ++k) {
				auto& route = def->emitters[i].events[k];
				const auto ep = root + "/Emitters/" + std::to_string(i) + "/Events/" + std::to_string(k) + "/Emitter";
				const auto target
					= std::find_if(def->emitters.begin(), def->emitters.end(), [&](const auto& emitter) { return emitter.id == route.emitterId; });
				if (target == def->emitters.end()) {
					fail(ep, "unknown target emitter");
				}
				route.emitter = size_t(target - def->emitters.begin());
				if (target->renderer.screen != def->emitters[i].renderer.screen) {
					fail(ep, "event routes cannot cross screen/world render spaces");
				}
			}
		}
		for (auto& emitter : def->emitters) {
			std::sort(emitter.events.begin(), emitter.events.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
		}
		return def;
	} catch (const nlohmann::json::exception& e) {
		fail(root, e.what());
	}
}
}
