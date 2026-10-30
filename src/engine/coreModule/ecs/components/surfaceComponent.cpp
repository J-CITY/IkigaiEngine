#include "surfaceComponent.h"
#include "resourceModule/materialManager.h"
#include "surfaceModule/surfaceMaterial.h"
#include "surfaceModule/surfaceRuntime.h"
#include <algorithm>

namespace IKIGAI::ECS {
struct SurfaceComponent::SurfaceUpdate {
	SURFACE::Settings Settings = {};
	std::shared_ptr<SURFACE::BackgroundJob> Job = {};
	std::optional<SURFACE::PreparedSurface> Prepared = {};
	std::shared_ptr<SURFACE::Terrain> Terrain = {};
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> Sources = {};
	size_t NextTile = 0;
	uint64_t Revision = 0;
	bool Ready = false;
};
SurfaceComponent::SurfaceComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "SurfaceComponent";
	apply({});
}
SurfaceComponent::SurfaceComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object) {
	__NAME__ = "SurfaceComponent";
	apply(static_cast<const Descriptor&>(descriptor).Surface);
}
SurfaceComponent::Descriptor SurfaceComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<SurfaceComponent>();
	descriptor.Surface = settings();
	return descriptor;
}
void SurfaceComponent::apply(const SURFACE::Settings& settings) {
	settings.validate();
	auto resolved = settings;
	for (unsigned index = 0; index < resolved.Layers.size(); ++index) {
		resolved.Layers[index].Id = settings.stableLayerId(index);
	}
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> materials;
	for (const auto& layer : settings.Layers) {
		materials.push_back(
			layer.Material.empty() ? nullptr : RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(layer.Material));
		if (!layer.Material.empty() && !materials.back()) {
			throw std::runtime_error("Cannot load surface material: " + layer.Material);
		}
	}
	materials.push_back(settings.Water.Material.empty()
			? nullptr
			: RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(settings.Water.Material));
	if (!settings.Water.Material.empty() && !materials.back()) {
		throw std::runtime_error("Cannot load water material: " + settings.Water.Material);
	}
	if (settings.Blend.Enabled) {
		SURFACE::validateSurfaceMaterials(resolved, materials);
	}
	if (mTerrain && settings.BackgroundUpdates) {
		auto update = std::make_shared<SurfaceUpdate>();
		update->Settings = resolved;
		update->Sources = std::move(materials);
		update->Revision = mRequestedRevision + 1;
		update->Job = std::make_shared<SURFACE::BackgroundJob>([resolved, keys = mTerrain->residentRequests()](const auto& cancellation) -> std::any {
			return SURFACE::prepareSurface(resolved, keys, cancellation);
		});
		mUpdate = std::move(update);
		mSettings = resolved;
		++mRequestedRevision;
		mUpdateError.clear();
		return;
	}
	auto terrain = std::make_shared<SURFACE::Terrain>(resolved);
	if (settings.Blend.Enabled) {
		materials[0] = SURFACE::buildSurfaceMaterial(resolved, materials);
	}
	mTerrain = std::move(terrain);
	mMaterials = std::move(materials);
	mSettings = std::move(resolved);
	mUpdate.reset();
	mUpdateError.clear();
	frame = {};
	frame.Budget = settings.BuildBudget;
	mRevision = ++mRequestedRevision;
}
const SURFACE::Settings& SurfaceComponent::settings() const {
	return mSettings;
}
void SurfaceComponent::pollUpdate() {
	if (!mUpdate || mUpdate->Ready) {
		return;
	}
	try {
		auto& update = *mUpdate;
		if (update.Terrain) {
			update.Terrain->beginFrame();
		}
		if (!update.Prepared) {
			if (!update.Job->ready()) {
				return;
			}
			update.Prepared = std::any_cast<SURFACE::PreparedSurface>(update.Job->take());
			update.Job.reset();
			update.Terrain = std::make_shared<SURFACE::Terrain>(update.Settings);
			update.Terrain->canUpload();
			if (update.Settings.Blend.Enabled) {
				update.Sources[0] = SURFACE::buildSurfaceMaterial(update.Settings, update.Sources, &update.Prepared->CoveragePixels);
			}
		}
		while (update.NextTile < update.Prepared->Tiles.size() && frame.Budget && update.Terrain->canUpload()) {
			auto& tile = update.Prepared->Tiles[update.NextTile];
			update.Terrain->install(tile.Key, tile.Mesh);
			tile.Mesh = {};
			++update.NextTile;
			--frame.Budget;
		}
		update.Ready = update.NextTile == update.Prepared->Tiles.size();
	} catch (const std::exception& error) {
		mUpdateError = error.what();
		mUpdate.reset();
	}
}
void SurfaceComponent::publishUpdate(bool dependenciesReady) {
	if (!mUpdate || !mUpdate->Ready || !dependenciesReady) {
		return;
	}
	mTerrain = std::move(mUpdate->Terrain);
	mMaterials = std::move(mUpdate->Sources);
	mRevision = mUpdate->Revision;
	mUpdate.reset();
}
bool SurfaceComponent::updatePending() const {
	return bool(mUpdate);
}
const std::string& SurfaceComponent::updateError() const {
	return mUpdateError;
}
uint64_t SurfaceComponent::requestedRevision() const {
	return mRequestedRevision;
}
void SurfaceComponent::setEditLayers(const std::vector<SURFACE::EditLayer>& layers) {
	auto next = settings();
	auto previous = next.Edits;
	next.Edits = layers;
	apply(next);
	if (mUndoEdits.size() == 32) {
		mUndoEdits.erase(mUndoEdits.begin());
	}
	mUndoEdits.push_back(std::move(previous));
	mRedoEdits.clear();
}
void SurfaceComponent::addStroke(unsigned editLayerId, const SURFACE::BrushStroke& stroke) {
	auto layers = settings().Edits;
	auto found = std::find_if(layers.begin(), layers.end(), [&](const auto& layer) { return layer.Id == editLayerId; });
	if (found == layers.end()) {
		layers.push_back({ editLayerId });
		found = std::prev(layers.end());
	}
	found->Strokes.push_back(stroke);
	setEditLayers(layers);
}
bool SurfaceComponent::undoEdit() {
	if (mUndoEdits.empty()) {
		return false;
	}
	auto next = settings();
	auto previous = next.Edits;
	next.Edits = mUndoEdits.back();
	apply(next);
	mUndoEdits.pop_back();
	mRedoEdits.push_back(std::move(previous));
	return true;
}
bool SurfaceComponent::redoEdit() {
	if (mRedoEdits.empty()) {
		return false;
	}
	auto next = settings();
	auto previous = next.Edits;
	next.Edits = mRedoEdits.back();
	apply(next);
	mRedoEdits.pop_back();
	mUndoEdits.push_back(std::move(previous));
	return true;
}
SURFACE::Terrain& SurfaceComponent::terrain() {
	return *mTerrain;
}
const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& SurfaceComponent::materials() const {
	return mMaterials;
}
uint64_t SurfaceComponent::revision() const {
	return mRevision;
}
}
