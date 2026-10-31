#pragma once
#include "physicsModule/physicWorld.h"
#include "planeSlicer.h"
#include <serdepp/adaptor/nlohmann_json.hpp>

namespace IKIGAI::DESTRUCTION {
struct DestructibleDescriptor {
	int Version = 1;
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
	unsigned MaximumDepth = 8;
	float Lifetime = 30;
	float DamageThreshold = 1;
	float ImpactSpeed = 5;
	float ImpactImpulse = 10;
	template <class C> static auto serde(C& c, DestructibleDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&DestructibleDescriptor::Version, "Version")
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
			.field(&DestructibleDescriptor::MaximumDepth, "MaximumDepth", default_ { 8u })
			.field(&DestructibleDescriptor::Lifetime, "Lifetime", default_ { 30.f })
			.field(&DestructibleDescriptor::DamageThreshold, "DamageThreshold", default_ { 1.f })
			.field(&DestructibleDescriptor::ImpactSpeed, "ImpactSpeed", default_ { 5.f })
			.field(&DestructibleDescriptor::ImpactImpulse, "ImpactImpulse", default_ { 10.f });
	}
};
struct DestructibleDefinition {
	DestructibleDescriptor settings {};
	SliceSettings slicing {};
	FractureMesh mesh {};
	MATH::Vector3f center {};
	std::shared_ptr<const COLLISION::ConvexPolyhedron> hull {};
};
std::shared_ptr<const DestructibleDefinition> compileDefinition(DestructibleDescriptor settings, FractureMesh mesh);
}
