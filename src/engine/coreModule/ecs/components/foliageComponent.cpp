#include "foliageComponent.h"
#include "coreModule/ecs/componentReference.inl"
#include "renderModule/backends/interface/modelInterface.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "surfaceModule/surfaceJob.h"
#include "surfaceModule/surfaceRuntime.h"
#include <algorithm>
#include <unordered_set>

namespace IKIGAI::ECS {
FoliageComponent::FoliageComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "FoliageComponent";
	mDescriptor.Type = GetType<FoliageComponent>();
}
FoliageComponent::FoliageComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: FoliageComponent(object) {
	apply(static_cast<const Descriptor&>(descriptor));
}
FoliageComponent::Descriptor FoliageComponent::getDescriptor() const {
	return mDescriptor;
}
void FoliageComponent::apply(const Descriptor& descriptor) {
	if (descriptor.Species.size() > 32 || (!descriptor.Surface.empty() && descriptor.Surface.Component != GetComponentName<SurfaceComponent>())) {
		throw std::invalid_argument("Foliage requires a SurfaceComponent reference and at most 32 species");
	}
	std::unordered_set<std::string> names;
	unsigned instanceBudget = 0;
	std::vector<SpeciesRuntime> runtime;
	for (const auto& species : descriptor.Species) {
		species.validate();
		instanceBudget += species.MaxInstances;
		if (instanceBudget > 20000 || !names.insert(species.Name).second) {
			throw std::invalid_argument("Foliage requires unique species names and at most 20000 instances");
		}
		SpeciesRuntime current;
		for (const auto& lod : species.Lods) {
			current.Models.push_back(RESOURCES::ServiceManager::Get<RESOURCES::ModelLoader>().loadResource(
				lod.Model, lod.Model.ends_with(".model") ? RESOURCES::ELoadingType::RESOURCE : RESOURCES::ELoadingType::FILE));
			if (!current.Models.back()) {
				throw std::runtime_error("Cannot load foliage model: " + lod.Model);
			}
			current.LodMaterials.push_back(
				lod.Material.empty() ? nullptr : RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(lod.Material));
			if (!lod.Material.empty() && !current.LodMaterials.back()) {
				throw std::runtime_error("Cannot load foliage LOD material: " + lod.Material);
			}
		}
		for (const auto& material : species.Materials) {
			current.Materials.push_back(
				material.empty() ? nullptr : RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(material));
			if (!material.empty() && !current.Materials.back()) {
				throw std::runtime_error("Cannot load foliage material: " + material);
			}
		}
		for (const auto& model : current.Models) {
			auto materials = current.Materials;
			const auto& materialNames = model->getMaterialsNames();
			materials.resize(std::max(materials.size(), materialNames.size()));
			for (const auto& binding : species.MaterialBindings) {
				const auto found = std::find(materialNames.begin(), materialNames.end(), binding.Slot);
				if (found == materialNames.end() || std::count(materialNames.begin(), materialNames.end(), binding.Slot) != 1) {
					throw std::invalid_argument("Missing or ambiguous foliage material slot: " + binding.Slot);
				}
				auto material = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(binding.Material);
				if (!material) {
					throw std::runtime_error("Cannot load foliage material: " + binding.Material);
				}
				materials[size_t(found - materialNames.begin())] = std::move(material);
			}
			current.ModelMaterials.push_back(std::move(materials));
		}
		runtime.push_back(std::move(current));
	}
	for (size_t index = 0; index < descriptor.Species.size(); ++index) {
		for (size_t previous = 0; previous < mDescriptor.Species.size(); ++previous) {
			if (descriptor.Species[index].Name == mDescriptor.Species[previous].Name) {
				runtime[index].Placements = mRuntime[previous].Placements;
				break;
			}
		}
	}
	mDescriptor = descriptor;
	mDescriptor.Type = GetType<FoliageComponent>();
	mRuntime = std::move(runtime);
	mSurface = {};
	mSurfaceRevision = 0;
	mRequestedSurfaceRevision = 0;
	mPlacementJob.reset();
	mPreparedPlacements.reset();
	mUpdateError.clear();
}
void FoliageComponent::bind(SCENE_SYSTEM::Scene& scene) {
	bool changed = false;
	if (!mSurface) {
		mSurface = mDescriptor.Surface.empty() ? obj->getComponent<SurfaceComponent>() : Resolve<SurfaceComponent>(scene, mDescriptor.Surface);
		changed = true;
	}
	if (!mSurface) {
		if (mDescriptor.Species.empty() && mDescriptor.Surface.empty()) {
			return;
		}
		throw std::runtime_error("Foliage.Surface must reference an existing SurfaceComponent");
	}
	const bool updating = mSurface->updatePending();
	const auto revision = updating ? mSurface->requestedRevision() : mSurface->revision();
	if (!changed && mRequestedSurfaceRevision > revision && mSurfaceRevision == revision) {
		mPlacementJob.reset();
		mPreparedPlacements.reset();
		return;
	}
	if (changed || mRequestedSurfaceRevision != revision) {
		const auto& settings = updating ? mSurface->settings() : mSurface->terrain().settings();
		for (const auto& species : mDescriptor.Species) {
			SURFACE::validateScatter(settings, species);
		}
		mPreparedPlacements.reset();
		auto work = [settings, species = mDescriptor.Species](const auto& cancellation) -> std::any {
			std::vector<std::vector<SURFACE::Placement>> placements;
			for (const auto& type : species) {
				placements.push_back(SURFACE::scatter(settings, type, &cancellation));
			}
			return placements;
		};
		mPlacementJob.reset();
		if (settings.BackgroundUpdates) {
			mPlacementJob = std::make_shared<SURFACE::BackgroundJob>(std::move(work));
		}
		else {
			const std::atomic_bool cancellation = false;
			mPreparedPlacements = std::any_cast<std::vector<std::vector<SURFACE::Placement>>>(work(cancellation));
		}
		mRequestedSurfaceRevision = revision;
		mUpdateError.clear();
	}
	if (mPlacementJob && mPlacementJob->ready()) {
		try {
			mPreparedPlacements = std::any_cast<std::vector<std::vector<SURFACE::Placement>>>(mPlacementJob->take());
		} catch (const std::exception& error) {
			mUpdateError = error.what();
		}
		mPlacementJob.reset();
	}
	if (mPreparedPlacements && mRequestedSurfaceRevision == mSurface->revision()) {
		for (size_t index = 0; index < mRuntime.size(); ++index) {
			mRuntime[index].Placements = std::move((*mPreparedPlacements)[index]);
		}
		mSurfaceRevision = mRequestedSurfaceRevision;
		mPreparedPlacements.reset();
	}
}
bool FoliageComponent::preparedFor(uint64_t revision) const {
	return mSurfaceRevision == revision || (mRequestedSurfaceRevision == revision && mPreparedPlacements.has_value());
}
const std::string& FoliageComponent::updateError() const {
	return mUpdateError;
}
UTILS::WeakPtr<SurfaceComponent> FoliageComponent::surface() const {
	return mSurface;
}
const std::vector<FoliageComponent::SpeciesRuntime>& FoliageComponent::instances() const {
	return mRuntime;
}
const std::vector<SURFACE::FoliageSpecies>& FoliageComponent::species() const {
	return mDescriptor.Species;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::FoliageComponent>();
	return true;
}();
}
