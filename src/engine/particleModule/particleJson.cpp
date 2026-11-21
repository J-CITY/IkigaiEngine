#include "particles.h"
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace IKIGAI::PARTICLES {
namespace {
	using J = nlohmann::json;
	[[noreturn]] void fail(const std::string& p, const std::string& message) {
		throw std::invalid_argument(p + ": " + message);
	}
	const J& required(const J& j, const char* key, const std::string& p) {
		if (!j.contains(key))
			fail(p + "/" + key, "required field missing");
		return j.at(key);
	}
	void fields(const J& j, const std::string& p, std::initializer_list<const char*> allowed) {
		if (!j.is_object())
			fail(p, "expected object");
		for (auto it = j.begin(); it != j.end(); ++it) {
			bool found = false;
			for (auto name : allowed)
				if (it.key() == name) {
					found = true;
					break;
				}
			if (!found)
				fail(p + "/" + it.key(), "unknown field");
		}
	}
	float number(const J& j, const std::string& p) {
		if (!j.is_number())
			fail(p, "expected number");
		const float v = j.get<float>();
		if (!std::isfinite(v))
			fail(p, "expected finite number");
		return v;
	}
	float optional(const J& j, const char* k, float v, const std::string& p) {
		return j.contains(k) ? number(j.at(k), p + "/" + k) : v;
	}
	uint32_t integer(const J& j, const std::string& p, uint32_t max) {
		const double n = number(j, p);
		if (n < 0 || n > max || std::floor(n) != n)
			fail(p, "integer out of range");
		return uint32_t(n);
	}
	Value value(const J& j, int n, const std::string& p) {
		Value v { };
		if (n == 1) {
			v[0] = number(j, p);
			return v;
		}
		if (!j.is_array() || j.size() != size_t(n))
			fail(p, "expected " + std::to_string(n) + " channels");
		for (int i = 0; i < n; ++i)
			v[i] = number(j[i], p + "/" + std::to_string(i));
		return v;
	}
	Source constant(Value v, int n) {
		Source s;
		s.channels = n;
		s.min = s.max = v;
		return s;
	}
	void curves(const J& j, int n, std::array<MATH::CURVES::FloatCurve, 4>& out, const std::string& p) {
		fields(j, p, { "Keys" });
		const auto& keys = required(j, "Keys", p);
		if (!keys.is_array() || keys.empty() || keys.size() > 4096)
			fail(p + "/Keys", "expected 1..4096 keys");
		for (size_t i = 0; i < keys.size(); ++i) {
			const auto kp = p + "/Keys/" + std::to_string(i);
			const auto& k = keys[i];
			fields(k, kp, { "Time", "Value", "Interpolation", "OutControl", "InControl", "Easing" });
			const auto t = number(required(k, "Time", kp), kp + "/Time");
			const auto v = value(required(k, "Value", kp), n, kp + "/Value");
			MATH::CURVES::FloatKey base;
			base.time = t;
			const auto mode = k.value("Interpolation", std::string("linear"));
			if (mode == "step")
				base.interpolation = MATH::CURVES::SegmentInterpolation::Step;
			else if (mode == "linear")
				base.interpolation = MATH::CURVES::SegmentInterpolation::Linear;
			else if (mode == "bezier")
				base.interpolation = MATH::CURVES::SegmentInterpolation::Bezier;
			else if (mode == "easing") {
				base.interpolation = MATH::CURVES::SegmentInterpolation::Easing;
				const auto easing = integer(required(k, "Easing", kp), kp + "/Easing", uint32_t(MATH::CURVES::InterpolationType::CUSTOM) - 1);
				base.easing = static_cast<MATH::CURVES::InterpolationType>(easing);
			}
			else
				fail(kp + "/Interpolation", "unsupported interpolation");
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
			for (int c = 0; c < n; ++c)
				out[c].validate();
		} catch (const std::exception& e) {
			fail(p, e.what());
		}
	}
	Source source(const J& j, int n, const std::string& p, const Parameters& params) {
		fields(j, p, { "Mode", "Value", "Min", "Max", "Domain", "RandomChannels", "Parameter", "Keys" });
		Source s;
		s.channels = n;
		s.channel = hash(p);
		const auto mode = j.value("Mode", std::string("constant"));
		const auto domain = j.value("Domain", std::string("particleNormalizedAge"));
		if (domain == "particleNormalizedAge")
			s.domain = Domain::ParticleNormalized;
		else if (domain == "particleAge")
			s.domain = Domain::ParticleAge;
		else if (domain == "emitterNormalizedAge")
			s.domain = Domain::EmitterNormalized;
		else if (domain == "emitterAge")
			s.domain = Domain::EmitterAge;
		else
			fail(p + "/Domain", "unsupported domain");
		const auto channels = j.value("RandomChannels", std::string("linked"));
		if (channels != "linked" && channels != "independent")
			fail(p + "/RandomChannels", "expected linked or independent");
		s.independent = channels == "independent";
		if (mode == "constant")
			s.min = s.max = value(required(j, "Value", p), n, p + "/Value");
		else if (mode == "uniform") {
			s.mode = SourceMode::Uniform;
			s.min = value(required(j, "Min", p), n, p + "/Min");
			s.max = value(required(j, "Max", p), n, p + "/Max");
			for (int c = 0; c < n; ++c)
				if (s.min[c] > s.max[c])
					fail(p, "Min exceeds Max");
		}
		else if (mode == "curve") {
			s.mode = SourceMode::Curve;
			curves(J { { "Keys", required(j, "Keys", p) } }, n, s.lower, p);
		}
		else if (mode == "curveRange") {
			s.mode = SourceMode::CurveRange;
			curves(required(j, "Min", p), n, s.lower, p + "/Min");
			curves(required(j, "Max", p), n, s.upper, p + "/Max");
		}
		else if (mode == "parameter") {
			s.mode = SourceMode::Parameter;
			s.parameter = required(j, "Parameter", p).get<std::string>();
			if (!params.contains(s.parameter) || params.at(s.parameter).size() != size_t(n))
				fail(p + "/Parameter", "missing parameter or incompatible type");
		}
		else
			fail(p + "/Mode", "unsupported source");
		if (s.mode == SourceMode::Curve || s.mode == SourceMode::CurveRange) {
			if (s.domain == Domain::ParticleNormalized || s.domain == Domain::EmitterNormalized)
				for (int c = 0; c < n; ++c)
					for (const auto* curve : { &s.lower[c], &s.upper[c] })
						for (const auto& key : curve->keys)
							if (key.time < 0 || key.time > 1)
								fail(p, "normalized curve keys must lie in [0,1]");
		}
		return s;
	}
	Module module(const J& j, int d, bool mesh, bool spawn, const std::string& p, const Parameters& params) {
		fields(j, p,
			{ "Id", "Type", "Shape", "Offset", "Rotation", "Size", "Radius", "InnerRadius", "Height", "Sampling", "Lifetime", "Velocity", "Color",
			"Speed", "Angle", "Value", "Operation", "Orientation" });
		Module m;
		const auto id = required(j, "Id", p).get<std::string>();
		if (id.empty())
			fail(p + "/Id", "empty id");
		m.id = hash(id);
		const auto type = required(j, "Type", p).get<std::string>();
		if (j.contains("Orientation") && type != "Initialize")
			fail(p + "/Orientation", "Orientation belongs to Initialize");
		auto src
			= [&](const char* name, int n, Value def) { return j.contains(name) ? source(j[name], n, p + "/" + name, params) : constant(def, n); };
		if (type == "Shape") {
			m.type = ModuleType::Shape;
			auto& s = m.shape;
			const std::map<std::string, ShapeType> types { { "point", ShapeType::Point }, { "segment", ShapeType::Segment },
				{ "rectangle", ShapeType::Rectangle }, { "circle", ShapeType::Circle }, { "ring", ShapeType::Ring }, { "box", ShapeType::Box },
				{ "sphere", ShapeType::Sphere }, { "cone", ShapeType::Cone }, { "cylinder", ShapeType::Cylinder } };
			const auto name = required(j, "Shape", p).get<std::string>();
			if (!types.contains(name))
				fail(p + "/Shape", "unsupported shape");
			s.type = types.at(name);
			if ((d == 2 && s.type >= ShapeType::Box) || (d == 3 && s.type >= ShapeType::Rectangle && s.type <= ShapeType::Ring))
				fail(p + "/Shape", "shape incompatible with dimension");
			if (j.contains("Offset"))
				s.offset = value(j["Offset"], d, p + "/Offset");
			if (j.contains("Size"))
				s.size = value(j["Size"], d, p + "/Size");
			if (j.contains("Rotation")) {
				if (d == 2)
					s.rotation[2] = number(j["Rotation"], p + "/Rotation");
				else
					s.rotation = value(j["Rotation"], 3, p + "/Rotation");
			}
			s.radius = optional(j, "Radius", 1, p);
			s.innerRadius = optional(j, "InnerRadius", 0, p);
			s.height = optional(j, "Height", 1, p);
			for (int c = 0; c < d; ++c)
				if (s.size[c] < 0)
					fail(p + "/Size", "negative size");
			if (s.radius < 0 || s.innerRadius < 0 || s.innerRadius > s.radius || s.height < 0)
				fail(p, "invalid shape dimensions");
			const auto sampling = j.value("Sampling", std::string("volume"));
			if (sampling != "volume" && sampling != "surface")
				fail(p + "/Sampling", "expected volume or surface");
			s.surface = sampling == "surface";
		}
		else if (type == "Initialize") {
			m.type = ModuleType::Initialize;
			if (!j.contains("Lifetime") || !j.contains("Size"))
				fail(p, "Initialize requires Lifetime and Size");
			m.lifetime = src("Lifetime", 1, { 1, 0, 0, 0 });
			m.velocity = src("Velocity", d, { });
			m.size = src("Size", mesh ? 3 : 2, { 1, 1, 1, 0 });
			m.color = src("Color", 4, { 1, 1, 1, 1 });
			m.rotation = src("Rotation", 1, { });
			m.orientation = src("Orientation", 3, { });
			if (mesh && j.contains("Rotation"))
				fail(p + "/Rotation", "mesh3D uses Orientation (Euler XYZ degrees)");
			if (!mesh && j.contains("Orientation"))
				fail(p + "/Orientation", "Orientation requires mesh3D");
			if ((m.lifetime.mode == SourceMode::Constant || m.lifetime.mode == SourceMode::Uniform) && m.lifetime.min[0] <= 0)
				fail(p + "/Lifetime", "lifetime must be positive");
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
			if (!types.contains(type))
				fail(p + "/Type", "unsupported module");
			m.type = types.at(type);
			if ((m.type == ModuleType::OrientationOverLife || m.type == ModuleType::AngularVelocity3D) && !mesh)
				fail(p + "/Type", "3D orientation modules require mesh3D");
			if ((m.type == ModuleType::RotationOverLife || m.type == ModuleType::AngularVelocity) && mesh)
				fail(p + "/Type", "mesh3D requires OrientationOverLife or AngularVelocity3D");
			const int n = (m.type == ModuleType::Acceleration || m.type == ModuleType::VelocityOverLife
							  || m.type == ModuleType::PositionOffsetOverLife || m.type == ModuleType::PositionOverLife)
				? d
				: (m.type == ModuleType::OrientationOverLife || m.type == ModuleType::AngularVelocity3D) ? 3
				: m.type == ModuleType::SizeOverLife													 ? (mesh ? 3 : 2)
				: m.type == ModuleType::ColorOverLife													 ? 4
																										 : 1;
			if (m.type != ModuleType::IntegrateMotion) {
				const char* key = (m.type == ModuleType::RadialVelocity || m.type == ModuleType::DirectionalVelocity) ? "Speed" : "Value";
				if (!j.contains(key))
					fail(p + "/" + key, "missing source");
				m.value = source(j[key], n, p + "/" + key, params);
			}
			if (m.type == ModuleType::DirectionalVelocity) {
				if (d != 2)
					fail(p, "DirectionalVelocity is 2D; use Velocity or RadialVelocity in 3D");
				m.rotation = src("Angle", 1, { });
			}
			const auto op = j.value("Operation", std::string("multiplyBase"));
			if (op != "multiplyBase" && op != "set")
				fail(p + "/Operation", "expected set or multiplyBase");
			m.multiplyBase = op == "multiplyBase";
		}
		if (spawn
			!= (m.type == ModuleType::Shape || m.type == ModuleType::Initialize || m.type == ModuleType::RadialVelocity
				|| m.type == ModuleType::DirectionalVelocity))
			fail(p, "module in wrong stage");
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

std::shared_ptr<const Definition> parse(const J& j, const std::string& asset) {
	const std::string root = asset.empty() ? "particle" : asset;
	const auto& p = root;
	try {
		fields(j, root, { "Version", "Dimension", "Parameters", "Emitters", "EventLimits" });
		if (integer(required(j, "Version", p), root + "/Version", 100) != 1)
			fail(root + "/Version", "unsupported version");
		auto def = std::make_shared<Definition>();
		if (j.contains("EventLimits")) {
			const auto& limits = j["EventLimits"];
			const auto lp = root + "/EventLimits";
			fields(limits, lp, { "MaxEventsPerStep", "MaxChildParticlesPerStep", "MaxDepth" });
			if (limits.contains("MaxEventsPerStep"))
				def->eventLimits.maxEventsPerStep = integer(limits["MaxEventsPerStep"], lp + "/MaxEventsPerStep", 65536);
			if (!def->eventLimits.maxEventsPerStep)
				fail(lp + "/MaxEventsPerStep", "must be positive");
			if (limits.contains("MaxChildParticlesPerStep"))
				def->eventLimits.maxChildParticlesPerStep = integer(limits["MaxChildParticlesPerStep"], lp + "/MaxChildParticlesPerStep", 65536);
			if (limits.contains("MaxDepth"))
				def->eventLimits.maxDepth = integer(limits["MaxDepth"], lp + "/MaxDepth", 32);
		}
		const auto dim = required(j, "Dimension", p).get<std::string>();
		if (dim != "2D" && dim != "3D")
			fail(root + "/Dimension", "expected 2D or 3D");
		def->dimension = dim == "2D" ? 2 : 3;
		if (j.contains("Parameters")) {
			if (!j["Parameters"].is_object())
				fail(root + "/Parameters", "expected object");
			for (auto it = j["Parameters"].begin(); it != j["Parameters"].end(); ++it) {
				const auto p = root + "/Parameters/" + it.key();
				fields(it.value(), p, { "Type", "Default" });
				const auto t = required(it.value(), "Type", p).get<std::string>();
				int n = t == "float" ? 1 : t == "vec2" ? 2 : t == "vec3" ? 3 : (t == "vec4" || t == "color") ? 4 : 0;
				if (!n)
					fail(p + "/Type", "unsupported parameter type");
				const auto v = value(required(it.value(), "Default", p), n, p + "/Default");
				def->parameters[it.key()] = { v.begin(), v.begin() + n };
			}
		}
		const auto& emitters = required(j, "Emitters", p);
		if (!emitters.is_array() || emitters.empty() || emitters.size() > 128)
			fail(root + "/Emitters", "expected 1..128 emitters");
		std::set<std::string> ids;
		uint64_t totalCapacity = 0;
		for (size_t i = 0; i < emitters.size(); ++i) {
			const auto p = root + "/Emitters/" + std::to_string(i);
			const auto& e = emitters[i];
			fields(e, p,
				{ "Id", "SimulationSpace", "Capacity", "Timing", "Emission", "Spawn", "Update", "Renderer", "ScalePolicy", "InheritVelocity",
				"Events" });
			EmitterDefinition out;
			out.id = required(e, "Id", p).get<std::string>();
			if (out.id.empty() || !ids.insert(out.id).second)
				fail(p + "/Id", "empty or duplicate id");
			if (e.contains("Capacity"))
				out.capacity = integer(e["Capacity"], p + "/Capacity", 1000000);
			totalCapacity += out.capacity;
			if (!out.capacity || totalCapacity > 1000000)
				fail(p + "/Capacity", "effect capacity must be in 1..1000000");
			const auto space = e.value("SimulationSpace", std::string("world"));
			if (space != "world" && space != "local")
				fail(p + "/SimulationSpace", "expected local or world");
			out.local = space == "local";
			const auto scale = e.value("ScalePolicy", std::string("inherit"));
			if (scale != "inherit" && scale != "ignore")
				fail(p + "/ScalePolicy", "expected inherit or ignore");
			out.inheritScale = scale == "inherit";
			out.inheritVelocity = optional(e, "InheritVelocity", 0, p);
			const auto timing = e.value("Timing", J::object());
			fields(timing, p + "/Timing", { "Duration", "Delay", "Loops" });
			out.duration = optional(timing, "Duration", 1, p + "/Timing");
			out.delay = optional(timing, "Delay", 0, p + "/Timing");
			if (out.duration < .001 || out.delay < 0)
				fail(p + "/Timing", "Duration must be >= .001 and Delay >= 0");
			if (timing.contains("Loops"))
				out.loops = integer(timing["Loops"], p + "/Timing/Loops", 1000000);
			const auto emission = e.value("Emission", J::object());
			fields(emission, p + "/Emission", { "Rate", "Bursts", "Mode" });
			const auto mode = emission.value("Mode", std::string("automatic"));
			if (mode != "automatic" && mode != "eventsOnly")
				fail(p + "/Emission/Mode", "expected automatic or eventsOnly");
			out.eventsOnly = mode == "eventsOnly";
			if (out.eventsOnly && (emission.contains("Rate") || emission.contains("Bursts")))
				fail(p + "/Emission", "eventsOnly does not schedule Rate or Bursts");
			if (emission.contains("Rate"))
				out.rate = source(emission["Rate"], 1, p + "/Emission/Rate", def->parameters);
			if ((out.rate.mode == SourceMode::Curve || out.rate.mode == SourceMode::CurveRange) && out.rate.domain != Domain::EmitterAge
				&& out.rate.domain != Domain::EmitterNormalized)
				fail(p + "/Emission/Rate/Domain", "rate curves require an emitter domain");
			if (emission.contains("Bursts")) {
				if (!emission["Bursts"].is_array() || emission["Bursts"].size() > 1024)
					fail(p + "/Emission/Bursts", "expected at most 1024 bursts");
				for (size_t b = 0; b < emission["Bursts"].size(); ++b) {
					const auto bp = p + "/Emission/Bursts/" + std::to_string(b);
					const auto& v = emission["Bursts"][b];
					fields(v, bp, { "Time", "Count", "Interval" });
					Burst burst;
					burst.time = optional(v, "Time", 0, bp);
					burst.count = integer(required(v, "Count", bp), bp + "/Count", 65536);
					burst.interval = optional(v, "Interval", 0, bp);
					if (burst.time < 0 || burst.time > out.duration || burst.interval < 0 || (burst.interval > 0 && burst.interval < .001))
						fail(bp, "invalid burst time/interval");
					out.bursts.push_back(burst);
				}
			}
			std::set<uint64_t> modules;
			const auto& rendererJson = required(e, "Renderer", p);
			const auto rendererType = required(rendererJson, "Type", p + "/Renderer").get<std::string>();
			const bool mesh = rendererType == "mesh3D";
			if (mesh && def->dimension != 3)
				fail(p + "/Renderer/Type", "mesh3D requires Dimension 3D");
			int orientations = 0;
			int init = 0, shape = 0, solver = 0, path = 0;
			bool afterSolver = false, radial = false;
			for (bool spawning : { true, false }) {
				const auto key = spawning ? "Spawn" : "Update";
				if (!e.contains(key))
					continue;
				const auto& array = e[key];
				if (!array.is_array() || array.size() > 64)
					fail(p + "/" + key, "expected at most 64 modules");
				for (size_t k = 0; k < array.size(); ++k) {
					const auto mp = p + "/" + key + "/" + std::to_string(k);
					auto m = module(array[k], def->dimension, mesh, spawning, mp, def->parameters);
					if (m.type == ModuleType::OrientationOverLife || m.type == ModuleType::AngularVelocity3D)
						if (++orientations > 1)
							fail(mp, "use one orientation update mode per emitter");
					if (!modules.insert(m.id).second)
						fail(mp + "/Id", "duplicate module id");
					if (m.type == ModuleType::Initialize) {
						if (radial)
							fail(mp, "Initialize must precede RadialVelocity");
						++init;
					}
					if (m.type == ModuleType::Shape) {
						if (radial)
							fail(mp, "Shape must precede RadialVelocity");
						++shape;
					}
					if (m.type == ModuleType::RadialVelocity)
						radial = true;
					if (m.type == ModuleType::IntegrateMotion) {
						++solver;
						afterSolver = true;
					}
					if (m.type == ModuleType::PositionOverLife)
						++path;
					if ((m.type == ModuleType::Acceleration || m.type == ModuleType::Drag || m.type == ModuleType::VelocityOverLife) && afterSolver)
						fail(mp, "motion inputs must precede IntegrateMotion");
					(spawning ? out.spawn : out.update).push_back(std::move(m));
				}
			}
			if (init != 1 || shape > 1 || solver > 1 || path > 1 || solver + path != 1)
				fail(p, "need one Initialize and one motion mode, at most one Shape");
			out.kinematic = path == 1;
			if (out.kinematic)
				for (const auto& m : out.update)
					if (m.type == ModuleType::Acceleration || m.type == ModuleType::Drag || m.type == ModuleType::VelocityOverLife)
						fail(p + "/Update", "kinematic path conflicts with motion forces");
			const auto& r = required(e, "Renderer", p);
			const auto rp = p + "/Renderer";
			fields(r, rp,
				{ "Type", "RenderSpace", "Texture", "Atlas", "Piece", "Material", "Blend", "Layer", "Order", "DepthTest", "Alignment", "Pivot",
				"Columns", "Rows", "Mesh", "MeshIndex" });
			const auto rt = r.at("Type").get<std::string>();
			if (!mesh && rt != (def->dimension == 2 ? "sprite2D" : "billboard3D"))
				fail(rp + "/Type", "renderer incompatible with dimension");
			auto& rd = out.renderer;
			if (mesh) {
				rd.mesh = required(r, "Mesh", rp).get<std::string>();
				if (rd.mesh.empty())
					fail(rp + "/Mesh", "mesh asset path is empty");
				if (r.contains("MeshIndex"))
					rd.meshIndex = integer(r["MeshIndex"], rp + "/MeshIndex", 65535);
				for (const auto* field : { "Alignment", "Pivot" })
					if (r.contains(field))
						fail(rp + "/" + field, "billboard-only field");
			}
			else if (r.contains("Mesh") || r.contains("MeshIndex"))
				fail(rp, "Mesh and MeshIndex require mesh3D");
			rd.texture = r.value("Texture", std::string { });
			rd.atlas = r.value("Atlas", std::string { });
			rd.piece = r.value("Piece", std::string { });
			rd.material = r.value("Material", std::string { });
			if (!rd.texture.empty() && !rd.atlas.empty())
				fail(rp, "Texture and Atlas are mutually exclusive");
			const auto rs = r.value("RenderSpace", std::string(def->dimension == 2 ? "screen" : "world"));
			if (rs != "screen" && rs != "world")
				fail(rp + "/RenderSpace", "expected screen or world");
			rd.screen = rs == "screen";
			if (def->dimension == 3 && rd.screen)
				fail(rp + "/RenderSpace", "3D requires world space");
			rd.depthTest = r.value("DepthTest", !rd.screen);
			rd.layer = r.value("Layer", 0);
			rd.order = r.value("Order", 0);
			if (r.contains("Pivot")) {
				const auto v = value(r["Pivot"], 2, rp + "/Pivot");
				rd.pivot = { v[0], v[1] };
			}
			if (r.contains("Columns"))
				rd.columns = integer(r["Columns"], rp + "/Columns", 4096);
			if (r.contains("Rows"))
				rd.rows = integer(r["Rows"], rp + "/Rows", 4096);
			if (!rd.columns || !rd.rows)
				fail(rp, "flipbook dimensions must be positive");
			const auto blend = r.value("Blend", std::string("alpha"));
			if (blend == "alpha")
				rd.blend = Blend::Alpha;
			else if (blend == "additive")
				rd.blend = Blend::Additive;
			else if (blend == "premultiplied")
				rd.blend = Blend::Premultiplied;
			else
				fail(rp + "/Blend", "unsupported blend");
			const auto align = r.value("Alignment", std::string("camera"));
			if (align == "camera")
				rd.alignment = Alignment::Camera;
			else if (align == "velocity")
				rd.alignment = Alignment::Velocity;
			else if (align == "axisLocked")
				rd.alignment = Alignment::AxisLocked;
			else
				fail(rp + "/Alignment", "unsupported alignment");
			if (e.contains("Events")) {
				const auto& routes = e["Events"];
				if (!routes.is_array() || routes.size() > 64)
					fail(p + "/Events", "expected at most 64 routes");
				std::set<uint64_t> routeIds;
				for (size_t k = 0; k < routes.size(); ++k) {
					const auto ep = p + "/Events/" + std::to_string(k);
					const auto& route = routes[k];
					fields(route, ep, { "Id", "On", "Emitter", "Count", "Probability", "InheritVelocity", "InheritColor" });
					EventRoute event;
					const auto id = required(route, "Id", ep).get<std::string>();
					event.id = hash(id);
					if (id.empty() || !routeIds.insert(event.id).second)
						fail(ep + "/Id", "empty or duplicate route id");
					const auto on = required(route, "On", ep).get<std::string>();
					if (on != "birth" && on != "death")
						fail(ep + "/On", "expected birth or death");
					event.on = on == "birth" ? EventType::Birth : EventType::Death;
					event.emitterId = required(route, "Emitter", ep).get<std::string>();
					if (route.contains("Count"))
						event.count = integer(route["Count"], ep + "/Count", 4096);
					if (!event.count)
						fail(ep + "/Count", "must be positive");
					event.probability = optional(route, "Probability", 1, ep);
					if (event.probability < 0 || event.probability > 1)
						fail(ep + "/Probability", "expected [0,1]");
					event.inheritVelocity = optional(route, "InheritVelocity", 0, ep);
					event.inheritColor = route.value("InheritColor", false);
					out.events.push_back(std::move(event));
				}
			}
			def->emitters.push_back(std::move(out));
		}
		for (size_t i = 0; i < def->emitters.size(); ++i)
			for (size_t k = 0; k < def->emitters[i].events.size(); ++k) {
				auto& route = def->emitters[i].events[k];
				const auto ep = root + "/Emitters/" + std::to_string(i) + "/Events/" + std::to_string(k) + "/Emitter";
				const auto target
					= std::find_if(def->emitters.begin(), def->emitters.end(), [&](const auto& emitter) { return emitter.id == route.emitterId; });
				if (target == def->emitters.end())
					fail(ep, "unknown target emitter");
				route.emitter = size_t(target - def->emitters.begin());
				if (target->renderer.screen != def->emitters[i].renderer.screen)
					fail(ep, "event routes cannot cross screen/world render spaces");
			}
		for (auto& emitter : def->emitters)
			std::sort(emitter.events.begin(), emitter.events.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
		return def;
	} catch (const nlohmann::json::exception& e) {
		fail(root, e.what());
	}
}
}
