#include "backends/interface/uniformBufferInterface.h"
#include "coreModule/core/core.h"
#include "coreModule/ecs/components/environmentComponent.h"
#include "coreModule/ecs/components/vegetationInteractorComponent.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "gameRenderer.h"
#include "render.h"
#include "sceneModule/scene.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace IKIGAI::RENDER {
void GameRenderer::updateEnvironment(SCENE_SYSTEM::Scene& scene) {
	const auto old = mEnvironmentState;
	auto* component = ECS::EnvironmentSystem::active(scene);
	const auto quality = component ? component->quality().resolved() : ENVIRONMENT::QualitySettings {};
	const bool qualityChanged = quality != mEnvironmentQuality;
	mEnvironmentQuality = quality;
	mEnvironmentState = component ? component->runtime().gpu() : ENVIRONMENT::GpuState {};
	if (component) {
		mEnvironmentState.CloudOffset.z = float(quality.CloudSteps);
		mEnvironmentState.CloudOffset.w = float(quality.CloudLightSteps);
		mEnvironmentState.PreviousCloudOffset.z = float(quality.CloudShadowSteps);
	}
	if (component && component->sunObjectId() >= 0) {
		const auto sun = std::find(mLightObjectIds.begin(), mLightObjectIds.end(), component->sunObjectId());
		if (sun != mLightObjectIds.end()) {
			const auto index = size_t(sun - mLightObjectIds.begin());
			if (index < mLights.size() && mLights[index].type == 1) {
				mEnvironmentState.Wind.w = float(index + 1);
			}
		}
	}
	const auto shadowStage = std::find_if(mRenderPipeline->mStages.begin(), mRenderPipeline->mStages.end(),
		[](const auto& stage) { return stage->mName == "CloudShadowMap" && stage->mDescriptor.Enabled; });
	if (component && shadowStage != mRenderPipeline->mStages.end() && mRenderPipeline->mTextures.contains("CloudShadowMap")) {
		constexpr float shadowHalfExtent = 128.0f;
		const auto viewport = mContext.window->getPixelSize();
		const auto interval = std::max(unsigned((*shadowStage)->mDescriptor.UpdateInterval), quality.CloudShadowUpdateFrames);
		const auto sunDelta = mEnvironmentState.SunDirection - old.SunDirection;
		const bool shadowDiscontinuity = qualityChanged || old.Atmosphere.x != mEnvironmentState.Atmosphere.x
			|| std::abs(old.Timing.z - mEnvironmentState.Timing.z) > .5f
			|| sunDelta.x * sunDelta.x + sunDelta.y * sunDelta.y + sunDelta.z * sunDelta.z > .0025f
			|| old.CloudLayer.x != mEnvironmentState.CloudLayer.x || old.CloudLayer.y != mEnvironmentState.CloudLayer.y
			|| old.CloudLayer.z != mEnvironmentState.CloudLayer.z || old.CloudLayer.w != mEnvironmentState.CloudLayer.w
			|| old.CloudShape.x != mEnvironmentState.CloudShape.x || old.CloudShape.z != mEnvironmentState.CloudShape.z;
		if (!mCloudShadowAnchorValid || !(*shadowStage)->mExecuted || mFrameCount % interval == 0 || viewport.x != mCloudShadowViewport.x
			|| viewport.y != mCloudShadowViewport.y || shadowDiscontinuity
			|| std::abs(uboData.ViewPos.x - mCloudShadowAnchor.x) > shadowHalfExtent * .5f
			|| std::abs(uboData.ViewPos.z - mCloudShadowAnchor.y) > shadowHalfExtent * .5f) {
			mCloudShadowAnchor = { std::floor(uboData.ViewPos.x / 4.0f) * 4.0f, std::floor(uboData.ViewPos.z / 4.0f) * 4.0f };
			mCloudShadowViewport = { int(viewport.x), int(viewport.y) };
			mCloudShadowAnchorValid = true;
			mCloudShadowNeedsUpdate = true;
		}
		mEnvironmentState.CloudShadowMap = { mCloudShadowAnchor.x, mCloudShadowAnchor.y, shadowHalfExtent, 1.0f };
	}
	else {
		mCloudShadowAnchorValid = false;
	}
	mSurfaceWeatherUbo->setData(component ? component->surfaceWeather().gpu() : ENVIRONMENT::SurfaceWeatherGpu {});
	if (component) {
		const bool transmittance = mAtmosphereTransmittanceStage && mAtmosphereTransmittanceStage->mDescriptor.Enabled
			&& mRenderPipeline->mTextures.contains("AtmosphereTransmittance");
		const bool sky = transmittance && mAtmosphereSkyViewStage && mAtmosphereSkyViewStage->mDescriptor.Enabled
			&& mRenderPipeline->mTextures.contains("AtmosphereSkyView");
		const bool aerial = sky && component->effectiveSettings().AerialPerspective && mAtmosphereAerialStage
			&& mAtmosphereAerialStage->mDescriptor.Enabled && mRenderPipeline->mTextures.contains("AtmosphereAerial");
		const bool forceLuts = transmittance && (!mAtmosphereTransmittanceStage->mExecuted || (sky && !mAtmosphereSkyViewStage->mExecuted));
		const bool presetSettled = component->preset() != ENVIRONMENT::Preset::Custom && component->presetProgress() >= 1
			&& (old.Atmosphere.y != mEnvironmentState.Atmosphere.y || old.Atmosphere.z != mEnvironmentState.Atmosphere.z
				|| old.Atmosphere.w != mEnvironmentState.Atmosphere.w || old.CloudLayer.x != mEnvironmentState.CloudLayer.x
				|| old.CloudLayer.y != mEnvironmentState.CloudLayer.y || old.CloudLayer.z != mEnvironmentState.CloudLayer.z
				|| old.CloudLayer.w != mEnvironmentState.CloudLayer.w || old.CloudShape.x != mEnvironmentState.CloudShape.x
				|| old.CloudShape.y != mEnvironmentState.CloudShape.y || old.CloudShape.z != mEnvironmentState.CloudShape.z
				|| old.Wind.x != mEnvironmentState.Wind.x || old.Wind.y != mEnvironmentState.Wind.y || old.Wind.z != mEnvironmentState.Wind.z);
		// Forced bakes also restart the IBL interval, preventing an ordinary
		// cache update from immediately repeating the same expensive work.
		const auto update = mAtmosphereCache.update(
			component->simulationSettings(), mEnvironmentState, uboData.ViewPos.y, forceLuts, qualityChanged || presetSettled);
		mEnvironmentState.AtmosphereLut
			= { transmittance ? 1.0f : 0.0f, sky ? 1.0f : 0.0f, mAtmosphereCache.referenceHeight(), aerial ? 1.0f : 0.0f };
		// LUTs are updated independently of the slower IBL bake. All six IBL faces
		// and their convolutions execute together before the frame's consumers.
		if (transmittance && update.Transmittance) {
			mAtmosphereTransmittanceStage->mExecuted = false;
			mRenderPipeline->mIsInitialized = false;
		}
		if (sky && update.SkyView) {
			mAtmosphereSkyViewStage->mExecuted = false;
			mRenderPipeline->mIsInitialized = false;
		}
		if (aerial && (update.AerialPerspective || !mAtmosphereAerialStage->mExecuted || old.AtmosphereLut.w < .5f)) {
			mAtmosphereAerialStage->mExecuted = false;
			mRenderPipeline->mIsInitialized = false;
		}
		// Finish a requested preset at its target without waiting for the slower
		// IBL interval. Sun animation/advection alone keep the ordinary cache policy.
		if (update.Environment) {
			mRenderPipeline->invalidateResources({ "Environment", "SkyEnvironment" }, update.ResetHistory || qualityChanged);
		}
		if (update.ResetHistory || qualityChanged) {
			mRenderPipeline->mPresentationHistoryValid = false;
		}
	}
	else if (old.Atmosphere.x > .5f) {
		mRenderPipeline->invalidateResources({ "Environment", "SkyEnvironment" });
		mRenderPipeline->mPresentationHistoryValid = false;
		mAtmosphereCache = {};
	}
	mEnvironmentUbo->setData(mEnvironmentState);
	mInteractionState = {};
	struct Source {
		ECS::VegetationInteractorComponent* Component = nullptr;
		float Distance = 0;
	};
	std::vector<Source> sources;
	for (const auto& object : scene.getObjects()) {
		if (!object->isAlive() || !object->getIsActive()) {
			continue;
		}
		if (auto source = object->getComponent<ECS::VegetationInteractorComponent>()) {
			const auto delta = object->getTransform()->getWorldPosition() - uboData.ViewPos;
			sources.push_back({ source.get(), delta.dot(delta) });
		}
	}
	std::stable_sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) { return a.Distance < b.Distance; });
	size_t count = 0;
	for (const auto& source : sources) {
		for (size_t sample = 0; sample < source.Component->samples().size() && count < quality.InteractionSamples; ++sample) {
			const auto& current = source.Component->samples()[sample];
			const auto& previous = source.Component->previousSamples()[sample];
			mInteractionState.StartRadius[count] = current.StartRadius;
			mInteractionState.EndStrength[count] = current.EndStrength;
			mInteractionState.PreviousStartRadius[count] = previous.StartRadius;
			mInteractionState.PreviousEndStrength[count] = previous.EndStrength;
			mInteractionState.Owners[count].x = float(static_cast<int>(source.Component->obj->getID().getUniqueId()));
			++count;
		}
		if (count == quality.InteractionSamples) {
			break;
		}
	}
	mInteractionState.Count.x = float(count);
	mInteractionUbo->setData(mInteractionState);
}
void GameRenderer::bindEnvironment(Renderer& renderer, const ShaderReflection& reflection) {
	const auto& names = reflection.mNameToUniforms;
	if (names.contains("engine_Environment")) {
		renderer.setUniformBuffer("engine_Environment", mEnvironmentUbo);
	}
	if (names.contains("engine_Interactions")) {
		renderer.setUniformBuffer("engine_Interactions", mInteractionUbo);
	}
	if (names.contains("engine_SurfaceWeather")) {
		renderer.setUniformBuffer("engine_SurfaceWeather", mSurfaceWeatherUbo);
	}
	for (const auto& [binding, resource] : std::array<std::pair<const char*, const char*>, 7> {
		 { { "engine_CloudShape", "ShapeNoise" }, { "engine_CloudDetail", "DetailNoise" }, { "engine_CloudCurl", "CurlNoise" },
		 { "engine_CloudShadowMapTexture", "CloudShadowMap" }, { "engine_AtmosphereTransmittance", "AtmosphereTransmittance" },
		 { "engine_AtmosphereSkyView", "AtmosphereSkyView" }, { "engine_AtmosphereAerial", "AtmosphereAerial" } } }) {
		if (names.contains(binding)) {
			const auto texture = mRenderPipeline->mTextures.find(resource);
			const bool volume = std::string_view(resource) == "ShapeNoise" || std::string_view(resource) == "DetailNoise";
			renderer.setTexture(binding, texture == mRenderPipeline->mTextures.end() ? (volume ? mEmptyVolume : mEmptyTexture) : texture->second);
		}
	}
}
}
