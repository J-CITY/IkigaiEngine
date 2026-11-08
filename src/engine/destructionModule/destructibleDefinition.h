#pragma once
#include "fracturePreparation.h"
#include "physicsModule/physicWorld.h"
#include <cstdint>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <type_traits>

namespace IKIGAI::DESTRUCTION {
enum class GeometryMode { Convex, Concave };
enum class SupportPolicy { Disabled, Anchored };

struct SupportAnchorDesc {
	std::uint64_t id = 0;
	// Center and orientation refer to the authored model origin, before COM recentering.
	MATH::Vector3f center {};
	MATH::Vector3f halfExtents {};
	MATH::QuaternionF orientation { 0, 0, 0, 1 };
	bool enabled = true;

	template <class C> static auto serde(C& c, SupportAnchorDesc& v) {
		if constexpr (!C::is_serialize && std::is_same_v<typename C::Adaptor, nlohmann::json>) {
			validateJsonId(c.adaptor);
		}
		serde::serde_struct(c, v)
			.field(&SupportAnchorDesc::id, "Id")
			.field(&SupportAnchorDesc::center, "Center")
			.field(&SupportAnchorDesc::halfExtents, "HalfExtents")
			.field(&SupportAnchorDesc::orientation, "Orientation", QuaternionSerde {})
			.field(&SupportAnchorDesc::enabled, "Enabled", serde::attribute::default_ { true });
	}

private:
	struct QuaternionSerde {
		template <class T, class C, class Next> void from(C& c, T& orientation, std::string_view key, Next&& next) {
			MATH::Vector4f components {};
			next.from(c, components, key);
			orientation = { components.x, components.y, components.z, components.w };
		}
		template <class T, class C, class Next> void into(C& c, const T& orientation, std::string_view key, Next&& next) {
			MATH::Vector4f components { orientation.x, orientation.y, orientation.z, orientation.w };
			next.into(c, components, key);
		}
	};
	static void validateJsonId(const nlohmann::json& anchor);
};

struct DestructibleDescriptor {
	int Version = 1;
	GeometryMode Geometry = GeometryMode::Convex;
	std::string Model {};
	std::vector<std::string> Materials {};
	std::string InteriorMaterial {};
	float Mass = 1;
	bool Static = true;
	float Friction = .5f;
	float Restitution = .1f;
	float RelativeEpsilon = 1e-6f;
	float MinimumVolume = 1e-6f;
	float InteriorUvScale = 1;
	unsigned MaximumTriangles = 2048;
	unsigned MaximumPieces = 32;
	unsigned MaximumConvexChildren = 32;
	unsigned MaximumDepth = 8;
	float Lifetime = 30;
	float DamageThreshold = 1;
	float ImpactSpeed = 5;
	float ImpactImpulse = 10;
	SupportPolicy Support = SupportPolicy::Disabled;
	std::vector<SupportAnchorDesc> SupportAnchors {};
	template <class C> static auto serde(C& c, DestructibleDescriptor& v) {
		using namespace serde::attribute;
		if constexpr (!C::is_serialize && std::is_same_v<typename C::Adaptor, nlohmann::json>) {
			validateJsonAnchors(c.adaptor);
		}
		serde::serde_struct(c, v)
			.field(&DestructibleDescriptor::Version, "Version")
			.field(&DestructibleDescriptor::Geometry, "Geometry", default_ { GeometryMode::Convex })
			.field(&DestructibleDescriptor::Model, "Model")
			.field(&DestructibleDescriptor::Materials, "Materials")
			.field(&DestructibleDescriptor::InteriorMaterial, "InteriorMaterial")
			.field(&DestructibleDescriptor::Mass, "Mass", default_ { 1.f })
			.field(&DestructibleDescriptor::Static, "Static", default_ { true })
			.field(&DestructibleDescriptor::Friction, "Friction", default_ { .5f })
			.field(&DestructibleDescriptor::Restitution, "Restitution", default_ { .1f })
			.field(&DestructibleDescriptor::RelativeEpsilon, "RelativeEpsilon", default_ { 1e-6f })
			.field(&DestructibleDescriptor::MinimumVolume, "MinimumVolume", default_ { 1e-6f })
			.field(&DestructibleDescriptor::InteriorUvScale, "InteriorUvScale", default_ { 1.f })
			.field(&DestructibleDescriptor::MaximumTriangles, "MaximumTriangles", default_ { 2048u })
			.field(&DestructibleDescriptor::MaximumPieces, "MaximumPieces", default_ { 32u })
			.field(&DestructibleDescriptor::MaximumConvexChildren, "MaximumConvexChildren", default_ { 32u })
			.field(&DestructibleDescriptor::MaximumDepth, "MaximumDepth", default_ { 8u })
			.field(&DestructibleDescriptor::Lifetime, "Lifetime", default_ { 30.f })
			.field(&DestructibleDescriptor::DamageThreshold, "DamageThreshold", default_ { 1.f })
			.field(&DestructibleDescriptor::ImpactSpeed, "ImpactSpeed", default_ { 5.f })
			.field(&DestructibleDescriptor::ImpactImpulse, "ImpactImpulse", default_ { 10.f })
			.field(&DestructibleDescriptor::Support, "Support", default_ { SupportPolicy::Disabled })
			.field(&DestructibleDescriptor::SupportAnchors, "SupportAnchors", default_ { std::vector<SupportAnchorDesc> {} });
		if constexpr (C::is_serialize && std::is_same_v<typename C::Adaptor, nlohmann::json>) {
			if (v.SupportAnchors.empty()) {
				c.adaptor["SupportAnchors"] = nlohmann::json::array();
			}
		}
	}

private:
	static void validateJsonAnchors(const nlohmann::json& descriptor);
};
struct DestructibleDefinition {
	DestructibleDescriptor settings {};
	SliceSettings slicing {};
	std::optional<ConcaveSettings> concave {};
	FractureMesh mesh {};
	PieceGeometry geometry {};
	SupportPolicy support = SupportPolicy::Disabled;
	std::vector<SupportAnchorDesc> supportAnchors {};
};
std::shared_ptr<const DestructibleDefinition> compileDefinition(DestructibleDescriptor settings, FractureMesh mesh);
}
