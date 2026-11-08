#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>

#include "renderModule/backends/interface/renderEnums.h"
#include "virtualGeometrySelection.h"

namespace IKIGAI::RENDER {
struct Drawable;
class MeshInterface;
class ModelInterface;
class Renderer;
class ShaderInterface;
class StorageBufferInterface;
class UniformBufferInterface;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::VG {
class VirtualMeshAsset;

struct VirtualGeometryShadowSettings {
	bool Enabled = false;
	uint32_t MaxClusterTests = 65536;
	uint32_t MaxDraws = 4096;
	float ThresholdScale = 1.0f;

	template <class Context> static void serde(Context& context, VirtualGeometryShadowSettings& value) {
		using serde::attribute::default_;
		serde::serde_struct(context, value)
			.field(&VirtualGeometryShadowSettings::Enabled, "Enabled", default_ { false })
			.field(&VirtualGeometryShadowSettings::MaxClusterTests, "MaxClusterTests", default_ { uint32_t { 65536 } })
			.field(&VirtualGeometryShadowSettings::MaxDraws, "MaxDraws", default_ { uint32_t { 4096 } })
			.field(&VirtualGeometryShadowSettings::ThresholdScale, "ThresholdScale", default_ { 1.0f });
	}
};

void ValidateVirtualGeometryShadowSettings(const VirtualGeometryShadowSettings& settings);

struct VirtualGeometryShadowStatistics {
	uint32_t Cascades = 0;
	uint32_t ClusterDraws = 0;
	uint32_t BudgetFallbacks = 0;
	uint64_t Triangles = 0;
};

class VirtualGeometryShadows final {
public:
	void prepare(SCENE_SYSTEM::Scene& scene, bool enabled);
	void endFrame();
	// Records a separate cut for one directional cascade. Any rejected caster stays on the fallback lane.
	void record(RENDER::Renderer& renderer, const VirtualGeometryView& view, const VirtualGeometryShadowSettings& settings,
		const std::shared_ptr<RENDER::ShaderInterface>& shader, const std::shared_ptr<RENDER::UniformBufferInterface>& shadowPass,
		RENDER::Depth depth);
	[[nodiscard]] const VirtualGeometryShadowStatistics& statistics() const;
	[[nodiscard]] bool replaces(const RENDER::Drawable& drawable) const;

private:
	struct Candidate {
		std::shared_ptr<VirtualMeshAsset> Asset;
		std::shared_ptr<RENDER::ModelInterface> Fallback;
		VirtualGeometryInstanceTransform Transform;
		std::vector<bool> CastsShadow;
		int OwnerId = -1;
		float Threshold = 1.0f;
		bool CullBack = true;
	};

	std::vector<Candidate> mCandidates;
	std::set<std::pair<int, const RENDER::MeshInterface*>> mReplaced;
	std::shared_ptr<RENDER::StorageBufferInterface> mArguments;
	uint32_t mArgumentCapacity = 0;
	VirtualGeometryShadowStatistics mStatistics;
};
}
