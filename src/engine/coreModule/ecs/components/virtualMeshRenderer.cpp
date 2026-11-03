#include "virtualMeshRenderer.h"

#include "renderModule/backends/interface/materialInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/serviceManager.h"
#include "virtualGeometryModule/runtime/virtualMeshAsset.h"
#include "virtualGeometryModule/runtime/virtualMeshLoader.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace IKIGAI::ECS {
VirtualMeshRenderer::VirtualMeshRenderer(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VirtualMeshRenderer";
}

VirtualMeshRenderer::VirtualMeshRenderer(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ComponentBase(object) {
	__NAME__ = "VirtualMeshRenderer";
	setErrorThreshold(descriptor.ErrorThreshold);
	setDebugMode(descriptor.DebugMode);
	mFallbackModelPath = descriptor.FallbackModel;
	setMaterials(descriptor.Materials);
	setPath(descriptor.Path);
}

VirtualMeshRenderer::VirtualMeshRenderer(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: VirtualMeshRenderer(object, static_cast<const Descriptor&>(descriptor)) { }

VirtualMeshRenderer::~VirtualMeshRenderer() = default;

VirtualMeshRenderer::Descriptor VirtualMeshRenderer::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<VirtualMeshRenderer>();
	descriptor.Path = mPath;
	descriptor.Materials = mMaterialPaths;
	descriptor.ErrorThreshold = mErrorThreshold;
	descriptor.DebugMode = mDebugMode;
	descriptor.FallbackModel = mFallbackModelPath;
	return descriptor;
}

void VirtualMeshRenderer::setPath(const std::string& path) {
	mPath = path;
	mAsset.reset();
	mFallbackModel.reset();
	mLoadError.clear();
	if (!path.empty()) {
		try {
			mAsset = RESOURCES::ServiceManager::Get<VG::VirtualMeshLoader>().loadResource(path, RESOURCES::ELoadingType::FILE);
		} catch (const std::exception& error) {
			mLoadError = error.what();
		}
		if (!mAsset && mLoadError.empty()) {
			mLoadError = "Virtual mesh " + path + ": loader returned no asset";
		}
	}
	if (!mLoadError.empty()) {
		std::cerr << "VG_LOAD_FAIL " << mLoadError << std::endl;
	}
	if (!mAsset && !mFallbackModelPath.empty()) {
		try {
			mFallbackModel = RESOURCES::ServiceManager::Get<RESOURCES::ModelLoader>().loadResource(
				mFallbackModelPath, mFallbackModelPath.ends_with(".model") ? RESOURCES::ELoadingType::RESOURCE : RESOURCES::ELoadingType::FILE);
		} catch (const std::exception& error) {
			std::cerr << "VG_FALLBACK_MODEL_FAIL " << mFallbackModelPath << ": " << error.what() << std::endl;
		}
	}
}

const std::string& VirtualMeshRenderer::getPath() const {
	return mPath;
}

const std::shared_ptr<VG::VirtualMeshAsset>& VirtualMeshRenderer::getAsset() const {
	return mAsset;
}

const std::string& VirtualMeshRenderer::getLoadError() const {
	return mLoadError;
}

void VirtualMeshRenderer::setMaterials(const std::vector<std::string>& paths) {
	mMaterialPaths = paths;
	mMaterials.clear();
	auto& loader = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>();
	for (const auto& path : paths) {
		mMaterials.push_back(path.empty() ? nullptr : loader.loadResource(path));
	}
}

void VirtualMeshRenderer::setMaterial(uint32_t slot, std::shared_ptr<RENDER::MaterialInterface> material) {
	if (slot >= mMaterials.size()) {
		mMaterials.resize(slot + 1);
		mMaterialPaths.resize(slot + 1);
	}
	mMaterialPaths[slot] = material ? material->getPath() : std::string {};
	mMaterials[slot] = std::move(material);
}

std::shared_ptr<RENDER::MaterialInterface> VirtualMeshRenderer::getMaterial(uint32_t slot) const {
	if (slot < mMaterials.size() && mMaterials[slot]) {
		return mMaterials[slot];
	}
	return mMaterials.empty() ? nullptr : mMaterials.front();
}

const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& VirtualMeshRenderer::getMaterials() const {
	return mMaterials;
}

void VirtualMeshRenderer::setErrorThreshold(float pixels) {
	if (!std::isfinite(pixels) || pixels < 0.0f) {
		throw std::invalid_argument("VirtualMeshRenderer ErrorThreshold must be a finite non-negative pixel count");
	}
	mErrorThreshold = pixels;
}

float VirtualMeshRenderer::getErrorThreshold() const {
	return mErrorThreshold;
}

void VirtualMeshRenderer::setDebugMode(VirtualMeshDebugMode mode) {
	mDebugMode = mode;
}

VirtualMeshDebugMode VirtualMeshRenderer::getDebugMode() const {
	return mDebugMode;
}

std::shared_ptr<RENDER::ModelInterface> VirtualMeshRenderer::getFallbackModel() const {
	if (mAsset) {
		return mAsset->fallbackModel();
	}
	return mFallbackModel;
}

MATH::Matrix4f VirtualMeshRenderer::advanceHistory(const MATH::Matrix4f& world, uint64_t frame) {
	if (mHistoryValid && mHistoryFrame == frame) {
		return mHistoryPreviousWorld;
	}
	const bool continuous = mHistoryValid && mHistoryFrame + 1 == frame;
	mHistoryPreviousWorld = continuous ? mHistoryWorld : world;
	mHistoryWorld = world;
	mHistoryFrame = frame;
	mHistoryValid = true;
	return mHistoryPreviousWorld;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VirtualMeshRenderer>();
	return true;
}();
}
