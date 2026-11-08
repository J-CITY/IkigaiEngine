#include "destructibleDefinition.h"
#include "collisionModule/compoundCollider.h"
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace IKIGAI::DESTRUCTION {
namespace {
	constexpr std::size_t MaxSupportAnchors = 64;

	void validateSupport(const DestructibleDescriptor& settings) {
		if (settings.Support == SupportPolicy::Disabled) {
			if (!settings.SupportAnchors.empty()) {
				throw std::invalid_argument("SupportAnchors requires Anchored support");
			}
			return;
		}
		if (settings.Support != SupportPolicy::Anchored || settings.Version != 2 || !settings.Static || settings.SupportAnchors.empty()
			|| settings.SupportAnchors.size() > MaxSupportAnchors) {
			throw std::invalid_argument("Anchored support requires a static version 2 destructible and 1 to 64 anchors");
		}
		std::unordered_set<std::uint64_t> ids;
		for (const auto& anchor : settings.SupportAnchors) {
			if (!anchor.id || !ids.insert(anchor.id).second) {
				throw std::invalid_argument("Support anchor Id must be nonzero and unique");
			}
			if (!std::isfinite(anchor.center.x) || !std::isfinite(anchor.center.y) || !std::isfinite(anchor.center.z)
				|| !validBoxCutout({ anchor.halfExtents, anchor.orientation })) {
				throw std::invalid_argument("Support anchor requires a finite Center, HalfExtents in [1e-5, 1e4] and a unit Orientation");
			}
		}
	}
}

void SupportAnchorDesc::validateJsonId(const nlohmann::json& anchor) {
	const auto& id = anchor.at("Id");
	if (!id.is_number_integer() || (!id.is_number_unsigned() && id.get<std::int64_t>() < 0)) {
		throw std::invalid_argument("Support anchor Id must be an unsigned integer");
	}
}

void DestructibleDescriptor::validateJsonAnchors(const nlohmann::json& descriptor) {
	if (const auto anchors = descriptor.find("SupportAnchors");
		anchors != descriptor.end() && (!anchors->is_array() || anchors->size() > MaxSupportAnchors)) {
		throw std::invalid_argument("SupportAnchors must be an array with at most 64 entries");
	}
}

std::shared_ptr<const DestructibleDefinition> compileDefinition(DestructibleDescriptor settings, FractureMesh mesh) {
	if ((settings.Version != 1 && settings.Version != 2) || (settings.Geometry != GeometryMode::Convex && settings.Geometry != GeometryMode::Concave)
		|| (settings.Version == 1 && settings.Geometry != GeometryMode::Convex) || settings.MaximumConvexChildren < 1
		|| settings.MaximumConvexChildren > COLLISION::CompoundCollider3D::MaxChildren || settings.Model.empty() || settings.Materials.empty()
		|| settings.Materials.size() > 254 || settings.InteriorMaterial.empty() || !std::isfinite(settings.Mass) || settings.Mass <= 0
		|| !std::isfinite(settings.Friction) || settings.Friction < 0 || !std::isfinite(settings.Restitution) || settings.Restitution < 0
		|| settings.Restitution > 1 || settings.MaximumPieces < 2 || settings.MaximumPieces > 128 || settings.MaximumDepth < 1
		|| settings.MaximumDepth > 16 || !std::isfinite(settings.Lifetime) || settings.Lifetime < 0 || !std::isfinite(settings.DamageThreshold)
		|| settings.DamageThreshold <= 0 || !std::isfinite(settings.ImpactSpeed) || settings.ImpactSpeed < 0 || !std::isfinite(settings.ImpactImpulse)
		|| settings.ImpactImpulse < 0) {
		throw std::invalid_argument("Invalid destructible descriptor: version, resources, mass, material, lifetime or budget");
	}
	validateSupport(settings);
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
	definition->support = s.Support;
	definition->supportAnchors = s.SupportAnchors;
	definition->slicing = { s.RelativeEpsilon, s.MinimumVolume, s.InteriorUvScale, static_cast<unsigned>(s.Materials.size()), s.MaximumTriangles };
	if (s.Geometry == GeometryMode::Concave) {
		definition->concave = ConcaveSettings { definition->slicing, s.MaximumPieces, s.MaximumConvexChildren };
	}
	definition->geometry = prepareGeometry(mesh, definition->slicing, definition->concave);
	if (definition->geometry.volume < s.MinimumVolume) {
		throw std::invalid_argument("Destructible source volume below MinimumVolume");
	}
	definition->mesh = std::move(mesh);
	return definition;
}
}
