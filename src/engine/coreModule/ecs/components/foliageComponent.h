#pragma once
#include "component.h"
#include "coreModule/ecs/componentReference.h"
#include "surfaceComponent.h"
#include "surfaceModule/foliage.h"
#include <optional>

namespace IKIGAI::RENDER {
class ModelInterface;
}
namespace IKIGAI::SURFACE {
class BackgroundJob;
}

namespace IKIGAI::ECS {
class FoliageComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = {};
		ComponentReference Surface = {};
		std::vector<SURFACE::FoliageSpecies> Species = {};
		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "FoliageComponentType")
				.field(&Descriptor::Surface, "Surface", default_ { ComponentReference {} })
				.field(&Descriptor::Species, "Species", default_ { std::vector<SURFACE::FoliageSpecies> {} });
		}
	};
	struct SpeciesRuntime {
		std::vector<std::shared_ptr<RENDER::ModelInterface>> Models = {};
		std::vector<std::shared_ptr<RENDER::MaterialInterface>> Materials = {};
		std::vector<std::vector<std::shared_ptr<RENDER::MaterialInterface>>> ModelMaterials = {};
		std::vector<std::shared_ptr<RENDER::MaterialInterface>> LodMaterials = {};
		std::vector<SURFACE::Placement> Placements = {};
	};
	explicit FoliageComponent(UTILS::Ref<Object> object);
	FoliageComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	void apply(const Descriptor& descriptor);
	void bind(SCENE_SYSTEM::Scene& scene);
	UTILS::WeakPtr<SurfaceComponent> surface() const;
	const std::vector<SpeciesRuntime>& instances() const;
	const std::vector<SURFACE::FoliageSpecies>& species() const;
	bool preparedFor(uint64_t revision) const;
	const std::string& updateError() const;
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	Descriptor mDescriptor = {};
	UTILS::WeakPtr<SurfaceComponent> mSurface = {};
	uint64_t mSurfaceRevision = 0;
	uint64_t mRequestedSurfaceRevision = 0;
	std::shared_ptr<SURFACE::BackgroundJob> mPlacementJob = {};
	std::optional<std::vector<std::vector<SURFACE::Placement>>> mPreparedPlacements = {};
	std::string mUpdateError = {};
	std::vector<SpeciesRuntime> mRuntime = {};
};
template <> inline std::string GetType<FoliageComponent>() {
	return "class IKIGAI::ECS::FoliageComponent";
}
template <> inline std::string GetComponentName<FoliageComponent>() {
	return "FoliageComponent";
}
}
