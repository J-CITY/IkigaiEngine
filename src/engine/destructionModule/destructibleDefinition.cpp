#include "destructibleDefinition.h"
#include <cmath>
#include <stdexcept>
namespace IKIGAI::DESTRUCTION {
std::shared_ptr<const DestructibleDefinition> compileDefinition(DestructibleDescriptor settings, FractureMesh mesh) {
	if (settings.Version != 1 || settings.Model.empty() || settings.Materials.empty() || settings.Materials.size() > 254
		|| settings.InteriorMaterial.empty() || !std::isfinite(settings.Mass) || settings.Mass <= 0 || !std::isfinite(settings.Friction)
		|| settings.Friction < 0 || !std::isfinite(settings.Restitution) || settings.Restitution < 0 || settings.Restitution > 1
		|| settings.MaximumPieces < 2 || settings.MaximumPieces > 128 || settings.MaximumDepth < 1 || settings.MaximumDepth > 16
		|| !std::isfinite(settings.Lifetime) || settings.Lifetime < 0 || !std::isfinite(settings.DamageThreshold) || settings.DamageThreshold <= 0
		|| !std::isfinite(settings.ImpactSpeed) || settings.ImpactSpeed < 0 || !std::isfinite(settings.ImpactImpulse) || settings.ImpactImpulse < 0) {
		throw std::invalid_argument("Invalid destructible descriptor: version, resources, mass, material, lifetime or budget");
	}
	for (const auto& path : settings.Materials) {
		if (path.empty()) {
			throw std::invalid_argument("Empty destructible material");
		}
	}
	for (auto material : mesh.materials) {
		if (material >= settings.Materials.size()) {
			throw std::invalid_argument("Missing exterior material slot");
		}
	}
	auto definition = std::make_shared<DestructibleDefinition>();
	definition->settings = std::move(settings);
	const auto& s = definition->settings;
	definition->slicing = { s.RelativeEpsilon, s.MinimumVolume, s.InteriorUvScale, static_cast<unsigned>(s.Materials.size()), s.MaximumTriangles };
	definition->hull = validateMesh(mesh, definition->slicing);
	if (definition->hull->volume < s.MinimumVolume) {
		throw std::invalid_argument("Destructible source volume below MinimumVolume");
	}
	definition->mesh = std::move(mesh);
	return definition;
}
}
