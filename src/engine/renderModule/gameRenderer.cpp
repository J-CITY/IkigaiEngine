#include "gameRenderer.h"
#include "coreModule/ecs/components/facialAnimationComponent.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "diagnostics/renderStatistics.h"
#include "environmentModule/surfaceWeather.h"
#include "frustum.h"
#include "gpuScene.h"
#include "indirectLighting/temporalDenoiseStage.h"
#include "instanceBatch.h"
#include "particles/particleRenderer.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
#include "renderGraph/computeStage.h"
#include "renderGraph/screenProbeStage.h"
#include "utilsModule/time/loadTimer.h"
#include "virtualGeometry/virtualGeometryPostStage.h"
#include "virtualGeometryModule/runtime/virtualGeometryRenderer.h"
#include "virtualGeometryModule/runtime/virtualMeshAsset.h"
#if USE_PHYSICS_3D
#include "physics/deformableRenderer.h"
#endif
#include "captureModule/screenshot.h"

#include "backends/interface/frameBufferInterface.h"
#include "coreModule/core/core.h"
#include "resourceModule/serviceManager.h"
#include <coreModule/graphicsWrapper.hpp>

#include "resourceModule/materialManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include "sceneModule/sceneManager.h"

#include "backends/interface/indexBufferInterface.h"
#include "backends/interface/uniformTypes.h"
#include "backends/interface/vertexBufferInterface.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/uniformBufferInterface.h"

#include "backends/driverFactory.h"
#include "backends/meshFactory.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "coreModule/gui/guiObject.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/light.h"
#include "renderModule/render.h"
#include "skeletalModule/animationOffset.h"
#include "skeletalModule/animationTransform.h"
#include "skeletalModule/iAnimationPlayable.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/time/time.h"
#include "vectorGraphicsModule/render/vectorRenderer.h"
#include "windowModule/window/window.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

#ifdef OCULUS
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/object.h"
#include "util_matrix.h"
#include "xr_linear.h"
#endif

namespace IKIGAI::RENDER {
std::shared_ptr<RayTracingProgram> LoadRayTracingProgram(Renderer&, const std::string&);

namespace {
#if defined(IKIGAI_USE_GPU_DRIVEN)
	constexpr std::string_view OrdinaryGeometryShader = "/shaders/render/portable/gbuffer.shader";
	constexpr std::string_view OrdinaryEmissiveGeometryShader = "/shaders/render/portable/gbufferEmissive.shader";

	bool SupportsGpuSceneInstancing(const MaterialInterface& material, std::string_view path, std::string_view alternate = {}) {
		const auto shader = material.getShader();
		if (!shader || material.getGPUInstances() != 1 || material.isBlendable()
			|| (shader->mPath != path && (alternate.empty() || shader->mPath != alternate))) {
			return false;
		}
		const auto& uniforms = shader->getReflection().mNameToUniforms;
		return uniforms.contains("engine_InstanceTransforms") && uniforms.contains("engine_InstanceMeta");
	}
#endif

	struct MorphWeightsGpu {
		std::array<int, 4> Meta {};
		std::array<MATH::Vector4f, 16> Current {};
		std::array<MATH::Vector4f, 16> Previous {};
	};
	static_assert(sizeof(MorphWeightsGpu) == 528);

	void setMorphWeight(std::array<MATH::Vector4f, 16>& values, std::size_t index, float weight) {
		auto& vector = values[index / 4];
		switch (index % 4) {
		case 0:
			vector.x = weight;
			break;
		case 1:
			vector.y = weight;
			break;
		case 2:
			vector.z = weight;
			break;
		default:
			vector.w = weight;
			break;
		}
	}
}

GameRenderer::GameRenderer(CORE::Core& context)
	: mContext(context) {
	Init();
}

VG::VirtualGeometryRenderer& GameRenderer::virtualGeometry() {
	if (!mVirtualGeometry) {
		mVirtualGeometry = std::make_shared<VG::VirtualGeometryRenderer>();
	}
	return *mVirtualGeometry;
}

const VG::VirtualGeometryShadowStatistics& GameRenderer::virtualGeometryShadowStatistics() const {
	return mVirtualGeometryShadows.statistics();
}

void GameRenderer::Init() {
	auto& render = mContext.render;

	mEmptyMaterial = nullptr;
	// mEmptyMaterial = render->createMaterial();
	// ShaderResource shaderRes;
	// mEmptyMaterial->setShader(render->createShader(shaderRes));
	// mEmptyMaterial->set("u_Diffuse", MATH::Vector4(1.f, 0.f, 1.f, 1.f));

#if !defined(IKIGAI_GL_HAS_SSBO)
	mLightSSBO = render->createStorageBuffer(nullptr, MAX_LIGHTS, sizeof(LightOGL));
#else
	mLightSSBO = render->createStorageBuffer(nullptr, 0, 0);
#endif
	mEmptyTexture = render->createTexture("/textures/empty.png");
	TextureResource volume;
	volume.texType = TextureType::TEXTURE_3D;
	volume.width = 1;
	volume.height = 1;
	volume.depth = 1;
	volume.channels = 4;
	volume.colorData = { 0, 0, 0, 0 };
	volume.useMipmap = false;
	mEmptyVolume = render->createTexture(volume);

	mEngineUbo = render->createUniformBuffer<EngineUBO>({});
	mEnvironmentUbo = render->createUniformBuffer<ENVIRONMENT::GpuState>({});
	mSurfaceWeatherUbo = render->createUniformBuffer<ENVIRONMENT::SurfaceWeatherGpu>({});
	mInteractionUbo = render->createUniformBuffer<InteractionGpu>({});
	mSurfaceUbo = render->createUniformBuffer<SurfaceGpu>({});
	mInstanceMetaUbo = render->createUniformBuffer<InstanceMetaGpu>({});
	const InstanceTransform emptyInstance;
	mEmptyInstanceBuffer = render->createStorageBuffer(&emptyInstance, 1, sizeof(emptyInstance));
	mBoneUbo = render->createUniformBuffer<BonesUBO>({});
	mMorphUbo = render->createUniformBuffer<MorphWeightsGpu>({});
	const MorphVertexDelta emptyMorph {};
	mEmptyMorphBuffer = render->createStorageBuffer(&emptyMorph, 1, sizeof(emptyMorph));
	mFrameUbo = render->createUniformBuffer<FrameUBO>({});
	if (DriverInterface::settings.backend == RenderSettings::Backend::VULKAN
		|| DriverInterface::settings.backend == RenderSettings::Backend::OPENGL) {
		mDrawFrameUbo = render->createUniformBuffer<FrameUBO>({});
	}
	mShadowUbo = render->createUniformBuffer<ShadowUBO>({});
	mShadowPassUbo = render->createUniformBuffer<MATH::Matrix4f>({});
	mPointShadowPassUbo = render->createUniformBuffer<PointShadowPassUBO>({});
	mCurrentShadowUbo = render->createUniformBuffer<CurrentShadowUBO>({});
	mFullscreenMesh = CreateMesh({ Vertex({ -1, -1, 0 }, { 0, 0 }), Vertex({ 3, -1, 0 }, { 2, 0 }), Vertex({ -1, 3, 0 }, { 0, 2 }) }, { 0, 1, 2 });

	RenderGraphPipeline::Descriptor desc;

	PipelineStage::Descriptor stageDesc;
	stageDesc.Draw = DrawContent::FORWARD;
	stageDesc.Name = "Forward";
	desc.Stages.push_back(stageDesc);

	if (!DriverInterface::settings.pipeline.empty()) {
		auto loaded = UTILS::FromJson<RenderGraphPipeline::Descriptor>(DriverInterface::settings.pipeline);
		if (loaded.isErr()) {
			throw std::runtime_error("Render pipeline: " + loaded.unwrapErr().text);
		}
		desc = loaded.unwrap();
	}
	std::cout << "Render pipeline: " << DriverInterface::settings.pipeline << "; stages=" << desc.Stages.size() << std::endl;
	setPipeline(std::make_unique<RenderGraphPipeline>(desc, *render, DriverInterface::settings.pipeline));
	mStartupPipelineAsset = mActivePipelineAsset = DriverInterface::settings.pipeline;
}

void GameRenderer::renderScene() {

	if (mContext.sceneManager->isTransitioning() || !mContext.sceneManager->hasCurrentScene()) {
#if defined(IKIGAI_USE_GPU_DRIVEN)
		mGpuScene.reset();
#endif
		renderLoadingScene(mContext.sceneManager->getPresentedScene());
		mPrimitives.clear();
		return;
	}
	auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
	std::unique_ptr<UTILS::LoadTimer> firstFrameTiming;
	if (Config::PROFILE_LOADING && mRenderedScene.lock() != scene.lifetime) {
		firstFrameTiming = std::make_unique<UTILS::LoadTimer>("scene-first-frame");
	}
	auto& render = mContext.render;

	auto camera = scene.findMainCamera();
	const auto asset = scene.renderPipelineAsset.empty() || HasCliRenderPipelineOverride() ? mStartupPipelineAsset : scene.renderPipelineAsset;
	if (asset != mActivePipelineAsset) {
		RenderGraphPipeline::Descriptor descriptor;
		if (asset.empty()) {
			PipelineStage::Descriptor forward;
			forward.Name = "Forward";
			forward.Draw = DrawContent::FORWARD;
			descriptor.Stages.push_back(forward);
		}
		else {
			auto loaded = UTILS::FromJson<RenderGraphPipeline::Descriptor>(asset);
			if (loaded.isErr()) {
				throw std::runtime_error("Scene render pipeline: " + loaded.unwrapErr().text);
			}
			descriptor = loaded.unwrap();
		}
		auto candidate = std::make_unique<RenderGraphPipeline>(descriptor, *render, asset);
		setPipeline(std::move(candidate));
		mActivePipelineAsset = asset;
		std::cout << "Scene render pipeline: " << asset << std::endl;
	}
	if (mRenderedScene.lock() != scene.lifetime) {
		resetTemporalHistory(HistoryReason::CameraCut);
		mPreviousCamera = nullptr;
#if defined(IKIGAI_USE_GPU_DRIVEN)
		mGpuScene.reset();
#endif
		mRenderedScene = scene.lifetime;
	}
	if (mRenderedTopologyRevision != scene.renderTopologyRevision) {
		resetTemporalHistory(HistoryReason::SceneTopology);
		mRenderedTopologyRevision = scene.renderTopologyRevision;
	}

	if (auto mainCameraComponent = mContext.sceneManager->getCurrentScene().findMainCamera()) {

		auto sz = mContext.window->getPixelSize();
		auto winWidth = sz.x;
		auto winHeight = sz.y;
		if (winWidth > 0 && winHeight > 0) {
			render->getDriver()->resize(winWidth, winHeight);
		}

		const auto& transform = mainCameraComponent->obj->getTransform();
		const auto& cameraPosition = transform->getWorldPosition();
		const auto& cameraRotation = transform->getWorldRotation();
		mainCameraComponent->getCamera().cacheMatrices(winWidth, winHeight, cameraPosition, cameraRotation);

		// const auto glState = mDriver->fetchGLState();
		renderScene(scene, *mainCameraComponent);
		// mDriver->applyStateMask(glState);
	}
	else {

		render->setClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		render->clear(true, true, false);
	}
	mPrimitives.clear();
	mFrameCount++;
}

void GameRenderer::renderLoadingScene(SCENE_SYSTEM::Scene* scene) {
	auto& render = *mContext.render;
	const auto size = mContext.window->getPixelSize();
	if (size.x <= 0 || size.y <= 0) {
		return;
	}
	render.getDriver()->resize(size.x, size.y);
	resetTemporalHistory(HistoryReason::SceneLoading);
	mPreviousCamera = nullptr;
	mSkinningPoses.clear();
	CAPTURE::ScreenshotService::Get().setSource({}, {});
	if (auto& driver = *render.getDriver(); driver.mFrameGeneration) {
		driver.mFrameGeneration->suspend();
		driver.mFrameGenerationStatus = driver.mFrameGeneration->status();
	}
	render.resetFrameBuffer();
	render.resetViewport();
	render.resetScissor();
	render.resetBlending();
	render.resetDepth();
	render.resetStencil();
	render.setClearColor(0.f, 0.f, 0.f, 1.f);
	render.clear(true, true, true);
	if (scene) {
		uboData.View = MATH::Matrix4f::Identity;
		uboData.Projection = MATH::Matrix4f::Identity;
		uboData.ViewportSize = { float(size.x), float(size.y) };
		uboData.Time = static_cast<float>(TIME::Timer::GetInstance().getTimeSinceStart().count());
		mContext.render->updateUniformBuffer(mEngineUbo, uboData);
		renderGui(*scene);
	}
}

const GameRenderer::SkinningPose* GameRenderer::skinningPose(const Drawable& drawable) {
	if (!drawable.skeleton || !drawable.animationPlayable || !drawable.mAnimLocalTransform || !drawable.mAnimGlobalTransform
		|| !drawable.mAnimOffset) {
		return nullptr;
	}
	if (drawable.skeleton->getNumJolts() > 128) {
		throw std::runtime_error("Skinning supports at most 128 joints");
	}
	auto [iterator, inserted] = mSkinningPoses.try_emplace(drawable.mAnimOffset);
	auto& pose = iterator->second;
	if (!inserted && pose.Frame == mFrameCount && pose.Skeleton == drawable.skeleton && pose.Playable == drawable.animationPlayable) {
		return &pose;
	}
	const bool continuous
		= !inserted && pose.Frame + 1 == mFrameCount && pose.Skeleton == drawable.skeleton && pose.Playable == drawable.animationPlayable;
	pose.Previous = pose.Current;
	auto* local = drawable.mAnimLocalTransform->generateTransforms(drawable.animationPlayable->getPose());
	auto* global = drawable.mAnimGlobalTransform->generateTransforms(local);
	auto* final = drawable.mAnimOffset->offset(global);
	pose.Current.fill(MATH::Matrix4f::Identity);
	std::copy_n(final->transforms, drawable.skeleton->getNumJolts(), pose.Current.begin());
	if (!continuous) {
		pose.Previous = pose.Current;
	}
	BonesUBO current {}, previous {};
	current.use = previous.use = 1;
	for (size_t i = 0; i < 128; ++i) {
		current.bones[i] = MATH::Matrix4f::Transpose(pose.Current[i]);
		previous.bones[i] = MATH::Matrix4f::Transpose(pose.Previous[i]);
	}
	pose.CurrentUbo = mContext.render->createUniformBuffer<BonesUBO>(current);
	pose.PreviousUbo = mContext.render->createUniformBuffer<BonesUBO>(previous);
	pose.Frame = mFrameCount;
	pose.Skeleton = drawable.skeleton;
	pose.Playable = drawable.animationPlayable;
	return &pose;
}

void GameRenderer::drawDrawable(const Drawable& drawable) {
	drawDrawable(drawable, nullptr);
}

#if defined(IKIGAI_USE_GPU_DRIVEN)
void GameRenderer::drawGpuScene(bool shadow) {
	for (const auto& batch : shadow ? mGpuScene->shadowDraws() : mGpuScene->draws()) {
		const Drawable drawable { MATH::Matrix4f::Identity, MATH::Matrix4f::Identity, batch.Mesh, batch.Material };
		drawDrawable(drawable, &batch.Buffers);
	}
}
#endif

void GameRenderer::drawDrawable(const Drawable& drawable, const GpuSceneDraw* indirect) {
	auto material = mOverrideMaterial ? mOverrideMaterial : drawable.material;
	if (!material || !drawable.mesh) {
		return;
	}
	if (mOverrideMaterial && drawable.material && drawable.material->hasShader() && drawable.material->getShader()->supportsPbrLayers()) {
		const auto passShader = mOverrideMaterial->getShader();
		const auto count = drawable.material->getShader()->compiledPbrLayerCount();
		if (passShader->pbrLayerVariant().empty()) {
			throw std::runtime_error("Pass shader has no layered PBR variant: " + passShader->mPath);
		}
		const auto key = std::make_tuple(reinterpret_cast<ShaderInterface::Id>(mOverrideMaterial.get()), passShader->generation(), count);
		auto& variant = mPbrPassMaterials[key];
		if (!variant) {
			auto descriptor = mOverrideMaterial->getDescriptor();
			descriptor.ShaderPath = passShader->pbrLayerVariant();
			descriptor.PbrLayers.Layers.resize(count);
			variant = mContext.render->createMaterial(descriptor);
		}
		material = variant;
	}
	if (mOverrideMaterial && drawable.material) {
		for (const auto& [key, value] : mOverrideDefaults) {
			material->set(key, value);
		}
		const auto copyUniform = [&](const std::string& target, const std::string& source) {
			const auto* value = drawable.material->findUniform(source);
			if (!value || !material->findUniform(target)) {
				return false;
			}
			material->set(target, *value);
			return true;
		};
		for (const auto& uniform : material->getShader()->getReflection().mUniforms) {
			if (material->isEngineUniform(uniform.mName)) {
				continue;
			}
			if (uniform.mMembers.empty()) {
				if (!copyUniform(uniform.mName, uniform.mName) && uniform.mName == "u_AlbedoMap") {
					copyUniform(uniform.mName, "u_DiffuseMap");
				}
			}
			else {
				for (const auto& member : uniform.mMembers) {
					const auto key = uniform.mName + member.mName;
					if (!copyUniform(key, key) && !copyUniform(key, member.mName)) {
						if (member.mName == "u_Albedo") {
							copyUniform(key, "data_UBOu_Diffuse");
						}
						else if (member.mName == "u_EmissiveColor" && material->findUniform(key)) {
							// The pass material is shared by all drawables: surfaces without emissive data must not inherit the previous one.
							material->set(key, MATH::Vector4f {});
						}
					}
				}
			}
		}
	}
	if (drawable.instances
		&& (!material->hasShader() || !material->getShader()->getReflection().mNameToUniforms.contains("engine_InstanceTransforms"))) {
		for (const auto& transform : drawable.instances->transforms()) {
			auto single = drawable;
			single.instances.reset();
			single.world = transform.World;
			single.prevWorld = transform.PreviousWorld;
			single.tint = transform.Tint;
			drawDrawable(single);
		}
		return;
	}

	static int skipLogs = 4;
	const bool canDraw = material && material->hasShader() && material->getGPUInstances() > 0 && drawable.mesh;
	if (skipLogs > 0 && !canDraw) {
		--skipLogs;
		IKIGAI_COUT("draw skip material=" << (material ? 1 : 0) << " shader=" << (material && material->hasShader() ? 1 : 0)
										  << " instances=" << (material ? material->getGPUInstances() : 0) << " mesh=" << (drawable.mesh ? 1 : 0));
	}
	if (material->hasShader() && material->getGPUInstances() > 0) {
		auto& render = mContext.render;

		const auto& passState = mOverrideMaterial ? mOverrideMaterial : material;
		render->setDepth({ passState->isDepthTest() ? passState->getDepthFunc() : DepthFunction::ALWAYS, passState->isDepthWriting() });
		// Pass overrides replace shaders, while the surface retains its
		// double-sided setting (including G-buffer and shadow passes).
		const auto& surface = drawable.material ? drawable.material : material;
		render->setCull(surface->isBackfaceCulling() ? CullFace::BACK : CullFace::NONE);
		if (passState->isBlendable()) {
			render->setBlending({ BlendMode::SRC_ALPHA, BlendMode::ONE_MINUS_SRC_ALPHA });
		}
		else {
			render->resetBlending();
		}
		// render->setCull(CullFace::NONE);

		render->setShader(material->getShader());
		material->bind(mEmptyTexture, true);
		if (mOverrideMaterial && mActiveStage) {
			const auto shader = material->getShader();
			if (mActiveStage->mResolved.Shader == shader) {
				for (const auto& texture : mActiveStage->mResolved.Textures) {
					render->setTexture(texture.Binding, *texture.Slot);
				}
			}
			else {
				// A layered surface can replace the stage's base shader with its variant.
				const auto& reflection = shader->getReflection();
				for (const auto& [name, value] : mActiveStage->mDescriptor.Uniforms) {
					if (const auto* texture = std::get_if<std::string>(&value)) {
						render->setTexture(
							reflection.mUniforms.at(reflection.mNameToUniforms.at(name)).mBind, mRenderPipeline->mTextures.at(*texture));
					}
				}
			}
		}

		// TODO: use render for it
		const auto model
			= DriverInterface::settings.backend == RenderSettings::Backend::OPENGL ? drawable.world : MATH::Matrix4f::Transpose(drawable.world);
		render->setPushConstant(IKIGAI::RENDER::ShaderType::VERTEX, 0, sizeof(MATH::Matrix4f), &model);

		const auto* pose = skinningPose(drawable);
		// mBoneUbo is the immutable empty pose; animated draws use pose-owned buffers.

		// std::static_pointer_cast<ShaderGl>(material->getShader())->setMat4("engine_Model.Projection",
		// MATH::Matrix4f::Transpose(uboData.Projection)); std::static_pointer_cast<ShaderGl>(material->getShader())->setMat4("engine_Model.View",
		// MATH::Matrix4f::Transpose(uboData.View));
		const auto& reflected = material->getShader()->getReflection().mNameToUniforms;
		bindEnvironment(*render, material->getShader()->getReflection());
		unsigned shellCount = 1;
		const auto& deformation = surface->deformation();
		if (deformation.Mode == DeformationMode::Fur) {
			const auto position = drawable.world * MATH::Vector3f {};
			const auto delta = position - uboData.ViewPos;
			const float distance = std::sqrt(delta.dot(delta));
			shellCount = std::max(2u,
				std::min(deformation.FurShells, mEnvironmentQuality.FurShellLimit)
					/ (distance > 40		? 4u
							: distance > 15 ? 2u
											: 1u));
		}
		if (reflected.contains("engine_Surface")) {
			auto state = surfaceGpu(deformation, shellCount);
			state.Pivot.w = float(drawable.OwnerId);
			mContext.render->updateUniformBuffer(mSurfaceUbo, state);
			render->setUniformBuffer("engine_Surface", mSurfaceUbo);
		}
		if (reflected.contains("engine_InstanceTransforms")) {
			const float instanceSource = indirect ? 1.f : drawable.instances ? float(drawable.instances->transforms().size()) : 0.f;
			const InstanceMetaGpu state { { instanceSource, float(shellCount), 0, 0 }, drawable.tint };
			mContext.render->updateUniformBuffer(mInstanceMetaUbo, state);
			render->setUniformBuffer("engine_InstanceMeta", mInstanceMetaUbo);
			render->setStorageBuffer("engine_InstanceTransforms",
				indirect				 ? indirect->Transforms
					: drawable.instances ? drawable.instances->buffer()
										 : mEmptyInstanceBuffer);
		}
		if (reflected.contains("engine_UBO")) {
			render->setUniformBuffer("engine_UBO", mEngineUbo);
		}
		if (reflected.contains("engine_Bones")) {
			render->setUniformBuffer("engine_Bones", pose ? pose->CurrentUbo : mBoneUbo);
		}
		if (reflected.contains("engine_PreviousBones")) {
			render->setUniformBuffer("engine_PreviousBones", pose ? pose->PreviousUbo : mBoneUbo);
		}
		if (reflected.contains("engine_MorphWeights") || reflected.contains("engine_MorphDeltas")) {
			MorphWeightsGpu morph {};
			const auto& targets = drawable.mesh->morphTargets();
			morph.Meta[0] = static_cast<int>(targets.size());
			morph.Meta[1] = static_cast<int>(drawable.mesh->getVertexCount());
			const auto* current = drawable.facialAnimation ? drawable.facialAnimation->weightsFor(*drawable.mesh) : nullptr;
			const auto* previous = drawable.facialAnimation ? drawable.facialAnimation->previousWeightsFor(*drawable.mesh) : nullptr;
			for (std::size_t index = 0; index < targets.size(); ++index) {
				setMorphWeight(morph.Current, index, current ? (*current)[index] : targets[index].DefaultWeight);
				setMorphWeight(morph.Previous, index, previous ? (*previous)[index] : targets[index].DefaultWeight);
			}
			if (reflected.contains("engine_MorphWeights")) {
				mContext.render->updateUniformBuffer(mMorphUbo, morph);
				render->setUniformBuffer("engine_MorphWeights", mMorphUbo);
			}
			if (reflected.contains("engine_MorphDeltas")) {
				render->setStorageBuffer("engine_MorphDeltas", drawable.mesh->morphBuffer() ? drawable.mesh->morphBuffer() : mEmptyMorphBuffer);
			}
		}
		if (reflected.contains("engine_Lights")) {
			render->setStorageBuffer("engine_Lights", mLightSSBO);
		}
		if (reflected.contains("engine_Frame")) {
			auto frame = mFrameData;
			frame.PreviousModel = MATH::Matrix4f::Transpose(drawable.prevWorld);
			if (mDrawFrameUbo) {
				// Vulkan snapshots the payload; OpenGL replaces the buffer store on update.
				// Both paths reuse the buffer object instead of allocating one per draw.
				mContext.render->updateUniformBuffer(mDrawFrameUbo, frame);
				render->setUniformBuffer("engine_Frame", mDrawFrameUbo);
			}
			else {
				render->setUniformBuffer("engine_Frame", render->createUniformBuffer<FrameUBO>(frame));
			}
		}
		if (reflected.contains("engine_ShadowData")) {
			render->setUniformBuffer("engine_ShadowData", mShadowUbo);
		}
		if (reflected.contains("engine_ShadowPass")) {
			render->setUniformBuffer("engine_ShadowPass", mShadowPassUbo);
		}
		if (reflected.contains("engine_PointShadowPass")) {
			render->setUniformBuffer("engine_PointShadowPass", mPointShadowPassUbo);
		}

		// glBindBufferBase(GL_UNIFORM_BUFFER, 0, std::static_pointer_cast<UniformBufferGl>(mEngineUbo)->getId());

		// drawable.mesh->bind();
		// if (drawable.mesh->getIndexCount() > 0) {
		//	// EBO
		//	glDrawElements(GL_TRIANGLES, drawable.mesh->getIndexCount(), GL_UNSIGNED_INT, nullptr);
		//
		// }
		// drawable.mesh->unbind();

		if (indirect) {
			render->setPrimitiveMode(PrimitiveMode::TRIANGLES);
			render->execute([mesh = drawable.mesh] { mesh->bind(); });
			render->drawIndexedIndirect(indirect->Arguments, 0, 1, sizeof(DrawIndexedIndirectCommand));
			render->execute([mesh = drawable.mesh] { mesh->unbind(); });
		}
		else {
			const unsigned instances = drawable.instances ? unsigned(drawable.instances->transforms().size()) : material->getGPUInstances();
			render->draw(drawable.mesh, PrimitiveMode::TRIANGLES, instances * shellCount);
		}

		// std::static_pointer_cast<ShaderGl>(material->getShader())->unbind();
		material->unbind();
	}
}
// TODO: split it
void GameRenderer::renderScene(IKIGAI::SCENE_SYSTEM::Scene& scene, IKIGAI::ECS::CameraComponent& cameraComponent) {
	mGpuProfiler.beginFrame();
	auto frameProfile = mGpuProfiler.measure("Frame");
	RenderStatistics::Get().beginFrame();
	auto renderSceneCpuProfile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("GameRenderer::renderScene") : nullptr;
#if USE_PHYSICS_3D
	if (!mDeformableRenderer) {
		mDeformableRenderer = std::make_shared<DeformableRenderer>();
	}
	mDeformableRenderer->update(scene);
#endif
	uboData.View = MATH::Matrix4f::Transpose(cameraComponent.getCamera().getViewMatrix());
	auto projection = cameraComponent.getCamera().getProjectionMatrix();
	if (DriverInterface::settings.backend == RenderSettings::Backend::VULKAN || DriverInterface::settings.backend == RenderSettings::Backend::METAL
		|| DriverInterface::settings.backend == RenderSettings::Backend::DIRECTX12) {
		// OpenGL-style projection (NDC z in [-1, 1]); Vulkan/Metal clip space uses z in [0, 1].
		// Y flip: DriverVk negative viewport height; DriverMetal flips viewport origin in applyFixedState.
		MATH::Matrix4f clipCorrection = MATH::Matrix4f::Identity;
		clipCorrection(2, 2) = 0.5f;
		clipCorrection(2, 3) = 0.5f;
		projection = clipCorrection * projection;
	}
	uboData.ViewPos = cameraComponent.obj->getTransform()->getWorldPosition();
	if (cameraComponent.isFrustumLightCulling()) {
		updateLightsInFrustum(scene, cameraComponent.getCamera().getFrustum());
	}
	else {
		updateLights(scene);
	}
	updateEnvironment(scene);
	const float previousTime = uboData.Time;
	uboData.Time = static_cast<float>(TIME::Timer::GetInstance().getTimeSinceStart().count());
	auto sz = mContext.window->getPixelSize();
	const MATH::Vector2i displaySize { int(sz.x), int(sz.y) };
	uboData.ViewportSize = MATH::Vector2f(sz.x, sz.y);
	uboData.FPS = static_cast<float>(TIME::Timer::GetInstance().getFPS());
	uboData.FrameCount = static_cast<int>(mFrameCount);
	if (mFrameCount % 60 == 0) {
		mRenderPipeline->pollSettings(*mContext.render);
	}
	mRenderPipeline->prepareUpscaler(*mContext.render, sz.x, sz.y);
	auto* presentationDriver = mContext.render->getDriver();
	const auto fgStages = std::count_if(mRenderPipeline->mStages.begin(), mRenderPipeline->mStages.end(),
		[](auto& s) { return s->mDescriptor.Enabled && s->mDrawContent == DrawContent::FRAME_GENERATION; });
	if (fgStages > 1) {
		throw std::runtime_error("A pipeline can have only one active FRAME_GENERATION stage");
	}
	presentationDriver->prepareFrameGeneration(fgStages == 1, mRenderPipeline->mRenderSize, { int(sz.x), int(sz.y) });
	mRenderPipeline->resize(*mContext.render, sz.x, sz.y);
	mRenderPipeline->pollResourceUpdates();
	const bool upscaleEnabled = bool(mRenderPipeline->mUpscaler);
	const auto renderSize = mRenderPipeline->mRenderSize;
	uboData.ViewportSize = { float(renderSize.x), float(renderSize.y) };
	auto unjitteredViewProjection = projection * cameraComponent.getCamera().getViewMatrix();
	const auto unjitteredProjection = projection;
	if (mPreviousCamera != &cameraComponent) {
		mRenderPipeline->mHistory.invalidate(HistoryReason::CameraCut);
	}
	else {
		float difference = 0;
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				difference = std::max(difference, std::abs(unjitteredViewProjection(row, column) - mPreviousUnjitteredViewProjection(row, column)));
			}
		}
		if (difference > 2.0f) {
			mRenderPipeline->mHistory.invalidate(HistoryReason::CameraCut);
		}
	}
	const bool taaEnabled = mRenderPipeline->mDescriptor.TemporalJitter
		&& std::any_of(mRenderPipeline->mStages.begin(), mRenderPipeline->mStages.end(),
			[](const auto& stage) { return stage->mDescriptor.Enabled && stage->mName == "TAA"; });
	mJitter = {};
	if (upscaleEnabled) {
		mJitter = mRenderPipeline->mUpscaler->jitter(mFrameCount, renderSize, displaySize);
		auto jitter = MATH::Matrix4f::Identity;
		jitter(0, 3) = 2 * mJitter.x / renderSize.x;
		jitter(1, 3) = (DriverInterface::settings.backend == RenderSettings::Backend::OPENGL ? 2.f : -2.f) * mJitter.y / renderSize.y;
		projection = jitter * projection;
	}
	else if (taaEnabled) {
		auto halton = [](unsigned index, unsigned base) {
			float fraction = 1, result = 0;
			while (index) {
				fraction /= base;
				result += fraction * (index % base);
				index /= base;
			}
			return result;
		};
		const auto index = static_cast<unsigned>(mFrameCount % 8) + 1;
		mJitter = { halton(index, 2) - .5f, halton(index, 3) - .5f };
		auto jitter = MATH::Matrix4f::Identity;
		jitter(0, 3) = mJitter.x * 2 / renderSize.x;
		jitter(1, 3) = (DriverInterface::settings.backend == RenderSettings::Backend::OPENGL ? 2.f : -2.f) * mJitter.y / renderSize.y;
		projection = jitter * projection;
	}
	uboData.Projection = MATH::Matrix4f::Transpose(projection);
	mContext.render->updateUniformBuffer(mEngineUbo, uboData);
	const auto viewProjection = projection * cameraComponent.getCamera().getViewMatrix();
	mFrameData.InvViewProjection = MATH::Matrix4f::Transpose(MATH::Matrix4f::Inverse(viewProjection));
	const bool temporalValid = mRenderPipeline->mHistory.isValid(HistoryEffect::Presentation);
	mFrameData.PreviousViewProjection = MATH::Matrix4f::Transpose(temporalValid ? mPreviousViewProjection : viewProjection);
	mFrameData.PreviousModel = MATH::Matrix4f::Identity;
	const float zeroToOne = DriverInterface::settings.backend == RenderSettings::Backend::OPENGL ? 0.0f : 1.0f;
	mFrameData.CameraParams = { cameraComponent.getNear(), cameraComponent.getFar(), zeroToOne, zeroToOne };
	// Jitter is expressed in texture pixels on every backend. G-buffer
	// velocity subtracts this delta so a stationary camera has zero motion;
	// RT denoising restores it when sampling its jittered history grid.
	mFrameData.FrameState = { temporalValid ? 1.0f : 0.0f, std::clamp(uboData.Time - previousTime, 0.0f, .25f),
		temporalValid ? (mJitter.x - mPreviousJitter.x) / renderSize.x : 0, temporalValid ? (mJitter.y - mPreviousJitter.y) / renderSize.y : 0 };
	const auto cameraFrameState = mFrameData.FrameState;
	mContext.render->updateUniformBuffer(mFrameUbo, mFrameData);

	std::vector<float> sun;
	for (const auto& light : mLights) {
		if (light.type == 1) {
			sun = { light.forward[0], light.forward[1], light.forward[2], light.color[0], light.color[1], light.color[2], light.intensity };
			break;
		}
	}
	if (mEnvironmentState.Atmosphere.x < .5f && mFrameCount > 0 && sun != mEnvironmentSun) {
		mRenderPipeline->invalidateResources({ "Environment", "SkyEnvironment" });
		mFrameData.FrameState.x = 0;
		mContext.render->updateUniformBuffer(mFrameUbo, mFrameData);
	}
	mEnvironmentSun = std::move(sun);

	const auto& cameraPosition = cameraComponent.obj->getTransform()->getWorldPosition();
	auto chunks = scene.findDrawables(cameraPosition, cameraComponent.getCamera(), nullptr, mEmptyMaterial);
	GpuSceneSnapshot* gpuSnapshot = nullptr;
#if defined(IKIGAI_USE_GPU_DRIVEN)
	GpuSceneSnapshot ordinary;
	const PipelineStage* ordinaryPost = nullptr;
	std::shared_ptr<TextureInterface> ordinaryHzb;
	if (mGpuSceneStage && mGpuSceneStage->mDescriptor.Enabled
		&& SupportsGpuSceneInstancing(*mGpuSceneStage->mMaterial, OrdinaryGeometryShader, OrdinaryEmissiveGeometryShader)) {
		if (!mGpuScene) {
			mGpuScene = std::make_unique<GpuScene>();
		}
		if (mGpuScene->available(*mContext.render)) {
			ordinary.UseBvhBounds = cameraComponent.getCamera().isFrustumGeometryBVHCulling();
			gpuSnapshot = &ordinary;
			if (!mRenderPipeline->mDescriptor.Execution.AsyncCompute) {
				ordinaryPost = FindActiveGeometryOcclusionPostStage(*mRenderPipeline, DrawContent::GPU_SCENE_OCCLUSION_POST, ordinaryHzb);
			}
		}
	}
	else {
		mGpuScene.reset();
	}
#endif
	auto shadowChunks = scene.findAndSortDrawables(cameraPosition, mEmptyMaterial, gpuSnapshot);
#if defined(IKIGAI_USE_GPU_DRIVEN)
	if (gpuSnapshot) {
		auto profile = mGpuProfiler.measure("Ordinary GPU cull");
		const auto& camera = cameraComponent.getCamera();
		const bool culling = ordinary.UseBvhBounds || camera.isFrustumGeometryCulling();
		mGpuScene->prepare(*mContext.render, ordinary, culling ? &camera.getFrustum() : nullptr, cameraPosition,
			{ ordinaryHzb, mEmptyTexture, viewProjection, mPreviousViewProjection, temporalValid });
	}
#endif
	{
		// Virtual geometry: LOD selection (GPU compute passes or CPU) and uploads before any stage; fallback meshes join
		// the regular drawables (shadows/RT always, the view when the cluster path is unavailable).
		VG::VirtualGeometryFrameInput virtualGeometryInput;
		virtualGeometryInput.SelectionPath = mRenderPipeline->mDescriptor.Execution.DrivenMode;
		virtualGeometryInput.View.CameraPosition = cameraPosition;
		virtualGeometryInput.View.Frustum = &cameraComponent.getCamera().getFrustum();
		virtualGeometryInput.View.Orthographic = cameraComponent.getProjectionMode() == Camera::ProjectionMode::ORTHOGRAPHIC;
		virtualGeometryInput.View.ProjectionScale = float(renderSize.y) * unjitteredProjection.getData()[1 * 4 + 1] * 0.5f;
		virtualGeometryInput.View.ZNear = cameraComponent.getNear();
		virtualGeometryInput.Frame = mFrameCount;
		virtualGeometryInput.ViewId = static_cast<uint64_t>(cameraComponent.obj->getID().getUniqueId());
		virtualGeometryInput.VisibilityRequested = bool(mRenderPipeline->mVgVisibilityShader);
		virtualGeometryInput.ViewMatrix = cameraComponent.getCamera().getViewMatrix();
		virtualGeometryInput.GpuProfiler = &mGpuProfiler;
		virtualGeometryInput.GeometryStageActive
			= std::any_of(mRenderPipeline->mStages.begin(), mRenderPipeline->mStages.end(), [](const auto& stage) {
				  return stage->mDescriptor.Enabled && stage->mDrawContent == DrawContent::DEFERRED && stage->mName == "Geometry";
			  });
		// Two-phase HZB occlusion (M4-V2): phase 1 may reject only when the post stage runs this frame.
		virtualGeometryInput.OcclusionPostStageActive
			= FindActiveGeometryOcclusionPostStage(*mRenderPipeline, DrawContent::VIRTUAL_GEOMETRY_POST, virtualGeometryInput.Hzb) != nullptr;
		virtualGeometryInput.HistoryValid = temporalValid;
		virtualGeometryInput.ViewProjection = viewProjection;
		virtualGeometryInput.PreviousViewProjection = mPreviousViewProjection;
		virtualGeometryInput.FallbackTexture = mEmptyTexture;
		virtualGeometry().prepareFrame(*mContext.render, scene, virtualGeometryInput, chunks, shadowChunks);
	}
	const bool useVgShadows = virtualGeometry().settings().Enabled && std::ranges::any_of(mRenderPipeline->mStages, [](const auto& stage) {
		return stage->mDescriptor.Enabled && bool(stage->mVgShadowShader);
	});
	mVirtualGeometryShadows.prepare(scene, useVgShadows);
	for (auto i = mSkinningPoses.begin(); i != mSkinningPoses.end();) {
		if (i->second.Frame + 1 < mFrameCount) {
			i = mSkinningPoses.erase(i);
		}
		else {
			++i;
		}
	}
	bool rayTracingActive = false, raySceneNeeded = false, progressive = false, rayLightsNeeded = false;
	for (const auto* stages : mRenderPipeline->stageGroups()) {
		for (const auto& stage : *stages) {
			if (stage->mDescriptor.Enabled && stage->mRayTracingProgram) {
				rayTracingActive = true;
				progressive |= stage->mDescriptor.Progressive;
				for (const auto& [name, source] : stage->mDescriptor.Bindings) {
					if (source == "engine_Lights") {
						rayLightsNeeded = true;
					}
					if (source == "@Scene" || source == "@Triangles" || source == "@Instances") {
						raySceneNeeded = true;
					}
				}
			}
		}
	}
	if (raySceneNeeded) {
		// The RT lane consumes stable cooked fallback meshes. Raster cascades may
		// skip selected draws but never change this input or its mesh identities.
		const auto& rayProxyChunks = shadowChunks;
		prepareRayTracingScene(rayProxyChunks);
	}
	else {
		mRenderPipeline->mHistory.beginFrame();
	}
	if (rayTracingActive) {
		auto lights = rayLightsNeeded ? scene.findLightData() : std::vector<LightOGL> {};
		if (lights.empty()) {
			lights.resize(1);
			lights[0].type = -1;
			lights[0].intensity = 0;
		}
		if (rayLightsNeeded
			&& (lights.size() != mRayLights.size() || std::memcmp(lights.data(), mRayLights.data(), lights.size() * sizeof(LightOGL)) != 0)) {
			mRayLightSSBO = mContext.render->createStorageBuffer(lights.data(), lights.size(), sizeof(LightOGL));
			mRayLights = std::move(lights);
			mRenderPipeline->mHistory.invalidate(HistoryReason::LightChange);
			mRaySceneChanged = true;
		}
		float difference = 0;
		for (int r = 0; r < 4; ++r) {
			for (int c = 0; c < 4; ++c) {
				difference = std::max(difference, std::abs(unjitteredViewProjection(r, c) - mPreviousUnjitteredViewProjection(r, c)));
			}
		}
		auto& history = mRenderPipeline->mHistory;
		if (progressive && (difference > 1e-6f || history.sceneMoved())) {
			// Progressive accumulation has no per-pixel rejection: any motion restarts it.
			history.invalidate(HistoryReason::ProgressiveMotion);
		}
		if (!history.allValid(HistoryRayTracedEffects)) {
			mRaySample = 0;
			mFrameData.FrameState.x = 0;
			mContext.render->updateUniformBuffer(mFrameUbo, mFrameData);
		}
	}
	const bool sdfMoved = !mRenderPipeline->mDescriptor.ScreenProbes.SdfAtlas.empty()
		&& UpdateSdfTrace(*mRenderPipeline, *mContext.render, scene, mContext.sceneManager->getCurrentSceneSourcePath());
	if (sdfMoved) {
		mRenderPipeline->mHistory.invalidate(
			HistoryReason::SceneDynamicPose, historyEffectBit(HistoryEffect::GlobalIllumination) | historyEffectBit(HistoryEffect::Reflections));
		mFrameData.FrameState.x = 0;
		mContext.render->updateUniformBuffer(mFrameUbo, mFrameData);
	}
	if (HasTemporalDenoisers(*mRenderPipeline)) {
		// Pipelines without a ray traced light list (hybrid-sp) detect light changes for their GI history here.
		if (!(rayTracingActive && rayLightsNeeded)) {
			DetectTemporalDenoiseLightChange(*mRenderPipeline, scene.findLightData());
		}
		UpdateTemporalDenoiseBuffers(*mContext.render, *mRenderPipeline, mFrameData.FrameState.x > .5f);
	}
	FrameGenerationInputs temporalCamera;
	temporalCamera.RenderSize = renderSize;
	temporalCamera.DisplaySize = displaySize;
	temporalCamera.View = cameraComponent.getCamera().getViewMatrix();
	temporalCamera.Projection = unjitteredProjection;
	temporalCamera.PreviousViewProjection = mPreviousUnjitteredViewProjection;
	temporalCamera.Position = uboData.ViewPos;
	temporalCamera.Right = { temporalCamera.View(0, 0), temporalCamera.View(0, 1), temporalCamera.View(0, 2) };
	temporalCamera.Up = { temporalCamera.View(1, 0), temporalCamera.View(1, 1), temporalCamera.View(1, 2) };
	temporalCamera.Forward = { -temporalCamera.View(2, 0), -temporalCamera.View(2, 1), -temporalCamera.View(2, 2) };
	temporalCamera.Jitter = mJitter;
	temporalCamera.PreviousJitter = mPreviousJitter;
	temporalCamera.Near = cameraComponent.getNear();
	temporalCamera.Far = cameraComponent.getFar();
	temporalCamera.VerticalFov = cameraComponent.getFov() * 3.14159265358979323846f / 180.f;
	temporalCamera.DeltaMilliseconds = mFrameData.FrameState.y * 1000.f;
	temporalCamera.RealFrameId = presentationDriver->mRealFrameId == UINT64_MAX ? mFrameCount : presentationDriver->mRealFrameId;
	temporalCamera.Reset = cameraFrameState.x < .5f || (presentationDriver->mFrameGeneration && !presentationDriver->mFrameGenerationStatus.Active);
	presentationDriver->setTemporalCamera(temporalCamera);
	RenderViewSnapshot viewSnapshot;
	viewSnapshot.ViewId = cameraComponent.obj->getID().getUniqueId().value();
	std::copy_n(temporalCamera.View.getData(), 16, viewSnapshot.View.begin());
	std::copy_n(temporalCamera.Projection.getData(), 16, viewSnapshot.Projection.begin());
	std::copy_n(temporalCamera.PreviousViewProjection.getData(), 16, viewSnapshot.PreviousViewProjection.begin());
	viewSnapshot.Position = { temporalCamera.Position.x, temporalCamera.Position.y, temporalCamera.Position.z };
	viewSnapshot.RenderSize = { uint32_t(renderSize.x), uint32_t(renderSize.y) };
	viewSnapshot.DisplaySize = { uint32_t(displaySize.x), uint32_t(displaySize.y) };
	viewSnapshot.Jitter = { mJitter.x, mJitter.y };
	viewSnapshot.Near = temporalCamera.Near;
	viewSnapshot.Far = temporalCamera.Far;
	mContext.render->addView(std::move(viewSnapshot));
	if (mFrameCount == 0) {
		for (const auto& chunk : chunks) {
			std::cout << "Render drawables: opaque=" << chunk.opaqueDrawablesForward.size() + chunk.opaqueDrawablesDeferred.size()
					  << " transparent=" << chunk.transparentDrawablesForward.size() + chunk.transparentDrawablesDeferred.size() << std::endl;
		}
	}
	mShadowData.LightIndices = { -1, -1, -1, 0 };
	bool rayFrameNeeded = rayTracingActive;
	for (const auto* stages : mRenderPipeline->stageGroups()) {
		for (const auto& stage : *stages) {
			if (stage->mDescriptor.Enabled && stage->mMaterial
				&& stage->mMaterial->getShader()->getReflection().mNameToUniforms.contains("engine_RayFrame")) {
				rayFrameNeeded = true;
			}
		}
	}
	if (rayFrameNeeded) {
		mRayFrameUbo = mContext.render->createUniformBuffer<RayFrameData>(rayFrameData());
	}
	UpdateScrollingDdgi(*mRenderPipeline, *mContext.render, cameraPosition);
	if (mRenderPipeline->mDescriptor.SurfaceCache) {
		const bool changed = mRenderPipeline->mDescriptor.ScreenProbes.SdfAtlas.empty()
			? mRaySceneChanged
			: UpdateSdfCacheLights(*mRenderPipeline, *mContext.render, scene.findLightData());
		UpdateSurfaceCache(*mRenderPipeline, *mContext.render, mContext.sceneManager->getCurrentSceneSourcePath(), changed || sdfMoved);
	}
	mContext.render->updateUniformBuffer(mShadowUbo, mShadowData);

	// Re-establish physical resources before applying disabled-stage aliases.
	for (const auto& [name, texture] : mRenderPipeline->mAllocatedTextures) {
		mRenderPipeline->mTextures.at(name) = texture;
	}
	std::function<void(PipelineStage*)> runStage;
	runStage = [&](PipelineStage* stage) {
		if (!stage->mDescriptor.Enabled || (upscaleEnabled && (stage->mName == "TAA" || stage->mName == "fxaa"))) {
			if (stage->mDrawContent == DrawContent::LIGHT_ACCUMULATION) {
				for (auto& init : mRenderPipeline->mLightPasses.at(stage->mDescriptor.LightPass).InitStages) {
					runStage(init.get());
				}
				return;
			}
			for (const auto& [output, input] : stage->mDescriptor.Bypass) {
				mRenderPipeline->mTextures.at(output) = mRenderPipeline->mTextures.at(input);
			}
			if (stage->mDescriptor.Bypass.empty() && stage->mFrameBuffer) {
				mContext.render->setFrameBuffer(stage->mFrameBuffer);
				mContext.render->setClearColor(stage->mDescriptor.ClearValue);
				mContext.render->clear(stage->mDescriptor.ClearColor, stage->mDescriptor.ClearDepth, false);
				mContext.render->setFrameBuffer(nullptr);
			}
			return;
		}
		if (stage->mDescriptor.Once && stage->mExecuted) {
			return;
		}
		if (stage->mDescriptor.UpdateInterval < 1) {
			throw std::runtime_error("Invalid stage update interval: " + stage->mName);
		}
		const bool cloudShadowUpdate = stage->mName == "CloudShadowMap" && mCloudShadowNeedsUpdate;
		if (stage->mName == "CloudShadowMap" && mEnvironmentState.CloudShadowMap.w > .5f && !cloudShadowUpdate) {
			return;
		}
		if (stage->mDescriptor.UpdateInterval > 1 && mFrameCount == 0 && !cloudShadowUpdate) {
			return;
		}
		if (mFrameCount % stage->mDescriptor.UpdateInterval != 0 && !cloudShadowUpdate) {
			return;
		}
		auto stageProfile = mGpuProfiler.measure(stage->mName);
		auto stageStatistics = RenderStatistics::Get().measureStage(stage->mName);
		auto stageCpuProfile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("Stage::" + stage->mName) : nullptr;
		const bool vgVisibility = stage->mDrawContent == DrawContent::VIRTUAL_GEOMETRY_VISIBILITY;
		if (vgVisibility || stage->mDrawContent == DrawContent::VIRTUAL_GEOMETRY_RESOLVE) {
			auto& render = *mContext.render;
			render.setFrameBuffer(stage->mFrameBuffer);
			const MATH::Vector2f size(stage->mFrameBuffer->getWidth(), stage->mFrameBuffer->getHeight());
			render.setViewport({ { 0, 0 }, size });
			render.resetScissor();
			uboData.ViewportSize = size;
			render.updateUniformBuffer(mEngineUbo, uboData);
			const VG::VirtualGeometryDrawBindings bindings { mEngineUbo, mFrameUbo, mEmptyTexture };
			if (vgVisibility) {
				render.clearIntColorAttachment(0, -1, -1);
				virtualGeometry().drawVisibility(render, bindings, mRenderPipeline->mVgVisibilityShader, stage->mFrameBuffer);
			}
			else {
				virtualGeometry().resolveVisibility(render, bindings, *mRenderPipeline->mVgVisibilityIds);
			}
			return;
		}
		if (mRenderPipeline->mVgVisibilityIds && stage->mDrawContent == DrawContent::TRANSPARENT_GEOMETRY
			&& stage->mDescriptor.Material == "/materials/render/portable/forwardTransparent.mat" && stage->mMaterial->isDepthWriting()) {
			throw std::invalid_argument("Visibility pipeline transparent material must keep depth writing disabled");
		}
		if (stage->mDrawContent == DrawContent::GPU_SCENE_OCCLUSION_POST) {
#if defined(IKIGAI_USE_GPU_DRIVEN)
			auto& render = *mContext.render;
			if (stage == ordinaryPost
				&& mGpuScene->recordOcclusionPost(render, mRenderPipeline->mTextures.at(stage->mDescriptor.Bindings.at("Hzb")))) {
				render.setFrameBuffer(stage->mFrameBuffer);
				const MATH::Vector2f size(float(stage->mFrameBuffer->getWidth()), float(stage->mFrameBuffer->getHeight()));
				uboData.ViewportSize = size;
				render.updateUniformBuffer(mEngineUbo, uboData);
				render.setViewport({ { 0, 0 }, size });
				render.resetScissor();
				bindStage(*mGpuSceneStage);
				setDrawStage(*mGpuSceneStage);
				drawGpuScene();
				mOverrideMaterial.reset();
				mActiveStage = nullptr;
				mGpuSceneStage->mMaterial->unbind();
				render.setFrameBuffer(nullptr);
			}
#endif
			stage->mExecuted = true;
			return;
		}
		if (stage->mDrawContent == DrawContent::VIRTUAL_GEOMETRY_POST) {
			// Virtual geometry phase 2: compute outside the pass, then draw into the G-buffer without clearing it.
			auto& render = *mContext.render;
			const auto hzb = mRenderPipeline->mTextures.at(stage->mDescriptor.Bindings.at("Hzb"));
			if (mVirtualGeometry && mVirtualGeometry->recordOcclusionPost(render, hzb, &mGpuProfiler)) {
				render.setFrameBuffer(stage->mFrameBuffer);
				const MATH::Vector2f size(float(stage->mFrameBuffer->getWidth()), float(stage->mFrameBuffer->getHeight()));
				uboData.ViewportSize = size;
				mContext.render->updateUniformBuffer(mEngineUbo, uboData);
				render.setViewport({ { 0, 0 }, size });
				render.resetScissor();
				mVirtualGeometry->drawOcclusionPost(render, { mEngineUbo, mFrameUbo, mEmptyTexture });
				render.setFrameBuffer(nullptr);
			}
			stage->mExecuted = true;
			return;
		}
		if (stage->mDrawContent == DrawContent::LIGHT_ACCUMULATION) {
			auto& pass = mRenderPipeline->mLightPasses.at(stage->mDescriptor.LightPass);
			bool rayShadows = false;
			if (!pass.Descriptor.ShadowProviderStage.empty()) {
				const auto provider = std::find_if(pass.PrepareStages.begin(), pass.PrepareStages.end(),
					[&](const auto& value) { return value->mName == pass.Descriptor.ShadowProviderStage; });
				if (provider == pass.PrepareStages.end()) {
					throw std::runtime_error("Unknown shadow provider stage: " + pass.Descriptor.ShadowProviderStage);
				}
				rayShadows = (*provider)->mDescriptor.Enabled && bool((*provider)->mRayTracingProgram);
			}
			for (auto& init : pass.InitStages) {
				runStage(init.get());
			}
			int used = 0, shadowed = 0, rasterCaptures = 0;
			struct RestoreCapture {
				int& index;
				~RestoreCapture() {
					index = -1;
				}
			} restoreCapture { mCaptureLightIndex };
			for (size_t i = 0; i < mLights.size(); ++i) {
				const auto& light = mLights[i];
				if (light.type < 0 || light.type > 2 || light.intensity <= 0) {
					continue;
				}
				const int id = i < mLightObjectIds.size() ? mLightObjectIds[i] : -1;
				if (!pass.Descriptor.LightIds.empty()
					&& std::find(pass.Descriptor.LightIds.begin(), pass.Descriptor.LightIds.end(), id) == pass.Descriptor.LightIds.end()) {
					continue;
				}
				mCaptureLightIndex = static_cast<int>(i);
				mShadowData.LightIndices = { -1, -1, -1, 0 };
				const bool hasShadow = light.shadowEnabled && (pass.Descriptor.ShadowBudget < 0 || shadowed < pass.Descriptor.ShadowBudget);
				float nearPlane = .1f, farPlane = 100.f;
				bool captured = false;
				for (auto& capture : pass.CaptureStages) {
					const auto& type = capture->mDescriptor.ShadowType;
					const int captureType = type == "directional" ? 1 : type == "spot" ? 2 : 0;
					if (captureType != light.type) {
						continue;
					}
					if (type == "pointCube") {
						auto scalar = [&](const std::string& key) {
							auto it = capture->mDescriptor.Uniforms.find(key);
							return it == capture->mDescriptor.Uniforms.end() ? std::get<float>(capture->mDefaults.at(key))
																			 : std::get<float>(it->second);
						};
						nearPlane = scalar("data_UBONear");
						farPlane = scalar("data_UBOFar");
					}
					else {
						farPlane = capture->mDescriptor.ShadowRange;
					}
					if (hasShadow && !rayShadows) {
						runStage(capture.get());
						++rasterCaptures;
						captured = true;
					}
				}
				if (light.shadowNear > 0) {
					nearPlane = light.shadowNear;
				}
				if (light.shadowFar > 0) {
					farPlane = light.shadowFar;
				}
				if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane) {
					throw std::runtime_error("Invalid per-light shadow range");
				}
				const bool shadowAvailable = captured || (hasShadow && rayShadows);
				if (shadowAvailable) {
					++shadowed;
				}
				mCurrentShadow = {};
				for (int c = 0; c < 4; ++c) {
					mCurrentShadow.Matrices[c] = mShadowData.Matrices[light.type == 2 ? 4 : c];
				}
				mCurrentShadow.CascadeSplits = mShadowData.CascadeSplits;
				mCurrentShadow.PositionRange = { light.pos[0], light.pos[1], light.pos[2], farPlane };
				mCurrentShadow.Settings = { mShadowData.Settings.x, nearPlane, farPlane, 0 };
				const bool volume = pass.Descriptor.VolumeLightLimit < 0 || used < pass.Descriptor.VolumeLightLimit;
				mCurrentShadow.LightInfo = { static_cast<float>(i), static_cast<float>(light.type), shadowAvailable ? 1.f : 0.f, volume ? 1.f : 0.f };
				// Each queued resolve owns a distinct immutable buffer snapshot.
				mCurrentShadowUbo = mContext.render->createUniformBuffer<CurrentShadowUBO>(mCurrentShadow);
				for (auto& prepare : pass.PrepareStages) {
					runStage(prepare.get());
				}
				for (auto& resolve : pass.ResolveStages) {
					runStage(resolve.get());
				}
				++used;
			}
			mCaptureLightIndex = -1;
			mShadowData.LightIndices = { -1, -1, -1, 0 };
			mContext.render->updateUniformBuffer(mShadowUbo, mShadowData);
			if (!stage->mExecuted) {
				std::cout << "LIGHT_ACCUMULATION " << pass.Descriptor.Name << " lights=" << used << " shadows=" << shadowed
						  << " shadow_provider=" << (rayShadows ? "ray" : "raster") << " raster_captures=" << rasterCaptures << std::endl;
			}
			stage->mExecuted = true;
			return;
		}
		if (stage->mDrawContent == DrawContent::COMPUTE) {
			if (stage->mCompute) {
				runComputeStage(*stage);
				return;
			}
			if (!stage->mDescriptor.Program.empty()) {
				runRayTracingStage(*stage);
				return;
			}
			if (!stage->mMaterial || stage->mDescriptor.DispatchTexture.empty()) {
				throw std::runtime_error("Compute requires material and DispatchTexture");
			}
			bindStage(*stage);
			const auto target = mRenderPipeline->mTextures.at(stage->mDescriptor.DispatchTexture);
			const auto& ref = stage->mMaterial->getShader()->getReflection();
			auto ceilDiv = [](size_t n, uint32_t d) {
				if (!d) {
					throw std::runtime_error("Invalid compute local size");
				}
				return static_cast<uint32_t>((n + d - 1) / d);
			};
			mContext.render->dispatch(ceilDiv(target->getWidth(), ref.mLocalSize[0]), ceilDiv(target->getHeight(), ref.mLocalSize[1]),
				ceilDiv(std::max<size_t>(1, target->getDepth()), ref.mLocalSize[2]));
			stage->mExecuted = true;
			std::cout << "Pipeline compute: " << stage->mName << std::endl;
			return;
		}
		if (stage->mDrawContent == DrawContent::RAY_TRACE) {
			runRayTracingStage(*stage);
			return;
		}
		if (stage->mDrawContent == DrawContent::FRAME_GENERATION) {
			if (DriverInterface::settings.frameGeneration.technology != FrameGenerationType::OFF
				&& !presentationDriver->mFrameGenerationStatus.Supported && stage->mDescriptor.Unsupported == "ERROR") {
				throw UnsupportedFrameGeneration(presentationDriver->mFrameGenerationStatus.Reason);
			}
			const auto& b = stage->mDescriptor.Bindings;
			FrameGenerationInputs f = temporalCamera;
			f.Color = mRenderPipeline->mTextures.at(b.at("Color"));
			f.UI = mRenderPipeline->mTextures.at(b.at("UI"));
			f.Depth = mRenderPipeline->mTextures.at(b.at("Depth"));
			f.MotionVectors = mRenderPipeline->mTextures.at(b.at("MotionVectors"));
			f.RenderSize = renderSize;
			f.DisplaySize = displaySize;
			f.Jitter = mJitter;
			f.PreviousJitter = mPreviousJitter;
			f.View = cameraComponent.getCamera().getViewMatrix();
			f.Projection = unjitteredProjection;
			f.PreviousViewProjection = mPreviousUnjitteredViewProjection;
			f.Position = uboData.ViewPos;
			f.Right = { f.View(0, 0), f.View(0, 1), f.View(0, 2) };
			f.Up = { f.View(1, 0), f.View(1, 1), f.View(1, 2) };
			f.Forward = { -f.View(2, 0), -f.View(2, 1), -f.View(2, 2) };
			f.Near = cameraComponent.getNear();
			f.Far = cameraComponent.getFar();
			f.VerticalFov = cameraComponent.getFov() * 3.14159265358979323846f / 180.f;
			f.DeltaMilliseconds = mFrameData.FrameState.y * 1000.f;
			f.RealFrameId = temporalCamera.RealFrameId;
			f.Reset = temporalCamera.Reset;
			presentationDriver->dispatchFrameGeneration(f);
			auto& screenshot = CAPTURE::ScreenshotService::Get();
			if (screenshot.pending() && screenshot.textureName().empty() && presentationDriver->mFrameGeneration
				&& presentationDriver->mFrameGenerationStatus.Active && presentationDriver->mFrameGeneration->composesUI()) {
				screenshot.setSource(f.Color, f.UI);
			}
			if (stage->mDescriptor.Unsupported == "ERROR" && !presentationDriver->mFrameGenerationStatus.Reason.empty()) {
				throw UnsupportedFrameGeneration(presentationDriver->mFrameGenerationStatus.Reason);
			}
			stage->mExecuted = true;
			return;
		}
		if (stage->mDrawContent == DrawContent::UPSCALE && mRenderPipeline->mUpscaler) {
			const auto& bindings = stage->mDescriptor.Bindings;
			auto texture = [&](const char* name, bool required = true) -> std::shared_ptr<TextureInterface> {
				auto found = bindings.find(name);
				if (found == bindings.end()) {
					if (required) {
						throw std::runtime_error(stage->mName + ": missing UPSCALE binding " + name);
					}
					return {};
				}
				return mRenderPipeline->mTextures.at(found->second);
			};
			UpscaleFrameInputs f;
			f.Color = texture("Color");
			f.Depth = texture("Depth");
			f.MotionVectors = texture("MotionVectors");
			f.Output = texture("Output");
			f.Exposure = texture("Exposure", false);
			f.Reactive = texture("Reactive", false);
			f.Transparency = texture("Transparency", false);
			f.RenderSize = renderSize;
			f.DisplaySize = displaySize;
			f.Jitter = mJitter;
			f.Near = cameraComponent.getNear();
			f.Far = cameraComponent.getFar();
			f.VerticalFov = cameraComponent.getFov() * 3.14159265358979323846f / 180.f;
			f.DeltaMilliseconds = mFrameData.FrameState.y * 1000.f;
			f.Reset = !mRenderPipeline->mHistory.isValid(HistoryEffect::Upscaler);
			f.Sharpness = mRenderPipeline->mUpscalerSettings.sharpness;
			try {
				mRenderPipeline->mUpscaler->dispatch(f);
				if (!stage->mExecuted || f.Reset) {
					std::cout << "UPSCALE_DISPATCH " << mRenderPipeline->mUpscalerStatus.Implementation << " reset=" << f.Reset << '\n';
				}
				mRenderPipeline->mHistory.markValid(historyEffectBit(HistoryEffect::Upscaler));
				stage->mExecuted = true;
				return;
			} catch (const UnsupportedUpscaler& error) {
				if (stage->mDescriptor.Unsupported == "ERROR") {
					throw;
				}
				mRenderPipeline->mUpscalerStatus.Supported = false;
				mRenderPipeline->mUpscalerStatus.Reason = error.what();
				mRenderPipeline->mUpscalerFailed = true;
				mRenderPipeline->mHistory.invalidate(HistoryReason::ResourceUpdate, historyEffectBit(HistoryEffect::Upscaler));
				std::cerr << "UPSCALER_FALLBACK " << error.what() << '\n';
				// Draw the stage's spatial fallback for this already-rendered frame.
			}
		}
		auto& render = *mContext.render;
		render.setFrameBuffer(stage->mFrameBuffer);
		const auto size = stage->mFrameBuffer ? MATH::Vector2f(stage->mFrameBuffer->getWidth(), stage->mFrameBuffer->getHeight())
											  : MATH::Vector2f(float(sz.x), float(sz.y));
		uboData.ViewportSize = size;
		mContext.render->updateUniformBuffer(mEngineUbo, uboData);
		render.setViewport({ { 0, 0 }, size });
		render.resetScissor();
		render.setClearColor(stage->mDescriptor.ClearValue);
		render.clear(stage->mDescriptor.ClearColor, stage->mDescriptor.ClearDepth, false);
		if (stage->mName == "PrepareFrameGenMotion") {
			// G-buffer motion excludes projection jitter for both native
			// TAA and upscalers. Frame generation uses that same motion.
			stage->mDefaults["data_UBOJitterDelta"] = MATH::Vector2f {};
		}
		if (stage->mName == "PrepareFrameGenMotion" || stage->mName == "PrepareUpscaleVelocity") {
			mFrameData.FrameState = cameraFrameState;
			mContext.render->updateUniformBuffer(mFrameUbo, mFrameData);
		}
		if (stage->mDescriptor.PresentationSink && stage->mDrawContent == DrawContent::QUAD) {
			stage->mDefaults["data_UBOCompositeUI"]
				= int(!(presentationDriver->mFrameGeneration && presentationDriver->mFrameGeneration->composesUI()));
		}
		if (stage->mMaterial) {
			bindStage(*stage);
		}
		setDrawStage(*stage);
		if (stage->mDrawContent == DrawContent::SHADOW) {
			GpuScene* gpuShadows = nullptr;
#if defined(IKIGAI_USE_GPU_DRIVEN)
			if (gpuSnapshot && mRenderPipeline->mDescriptor.Execution.GpuShadowViews) {
				gpuShadows = mGpuScene.get();
			}
#endif
			captureShadows(*stage, shadowChunks, cameraPosition, size, gpuShadows);
		}
		else if (stage->mDrawContent == DrawContent::QUAD || stage->mDrawContent == DrawContent::UPSCALE) {
			if (!stage->mMaterial) {
				throw std::runtime_error("Fullscreen stage requires Material: " + stage->mName);
			}
			render.draw(mFullscreenMesh, PrimitiveMode::TRIANGLES, 1);
		}
		else if (stage->mDrawContent == DrawContent::GUI) {
			renderGui(scene);
			if (stage->mDescriptor.IncludeEditorUI) {
				presentationDriver->renderPresentationUI(stage->mFrameBuffer);
			}
		}
		else if (stage->mDrawContent == DrawContent::PRIMITIVES_WORLD || stage->mDrawContent == DrawContent::PRIMITIVES_SCREEN) {
			mPrimitives.render(render, MATH::Matrix4f::Transpose(uboData.View * uboData.Projection), size,
				stage->mDrawContent == DrawContent::PRIMITIVES_WORLD ? PrimitiveSpace::World : PrimitiveSpace::Screen);
		}
		else if (stage->mDrawContent == DrawContent::WEATHER) {
			if (auto* environment = ECS::EnvironmentSystem::active(scene)) {
				if (!mParticleRenderer) {
					mParticleRenderer = std::make_shared<ParticleRenderer>();
				}
				mParticleRenderer->drawWeather(*environment, render, uboData.View, uboData.View * uboData.Projection, mEmptyTexture,
					mRenderPipeline->mTextures.at("CloudDepth"));
			}
		}
		else if (stage->mDrawContent == DrawContent::VECTOR_WORLD) {
			renderGui(scene, true);
		}
		else {
			const auto drawOpaque = [&](const Drawable& drawable) {
#if defined(IKIGAI_USE_GPU_DRIVEN)
				if (gpuSnapshot && stage == mGpuSceneStage && mGpuScene->contains(drawable)) {
					return;
				}
#endif
				drawDrawable(drawable);
			};
			for (auto& chunk : chunks) {
				if (stage->mDrawContent == DrawContent::FORWARD || stage->mDrawContent == DrawContent::DEFERRED
					|| stage->mDrawContent == DrawContent::SHADOW) {
					for (const auto& [distance, drawable] : chunk.opaqueDrawablesForward) {
						drawOpaque(drawable);
					}
					for (const auto& [distance, drawable] : chunk.opaqueDrawablesDeferred) {
						drawOpaque(drawable);
					}
				}
				if (stage->mDrawContent == DrawContent::FORWARD || stage->mDrawContent == DrawContent::TRANSPARENT_GEOMETRY) {
					for (const auto& [distance, drawable] : chunk.transparentDrawablesForward) {
						drawDrawable(drawable);
					}
					for (const auto& [distance, drawable] : chunk.transparentDrawablesDeferred) {
						drawDrawable(drawable);
					}
				}
			}
#if defined(IKIGAI_USE_GPU_DRIVEN)
			if (gpuSnapshot && stage == mGpuSceneStage) {
				drawGpuScene();
			}
#endif
			if (stage->mDrawContent == DrawContent::DEFERRED && stage->mName == "Geometry" && mVirtualGeometry) {
				mVirtualGeometry->drawGeometry(render, { mEngineUbo, mFrameUbo, mEmptyTexture });
			}
		}
		if (stage->mDrawContent == DrawContent::TRANSPARENT_GEOMETRY || stage->mDrawContent == DrawContent::FORWARD) {
			if (!mParticleRenderer) {
				mParticleRenderer = std::make_shared<ParticleRenderer>();
			}
			mParticleRenderer->drawWorld(scene, render, uboData.View, uboData.View * uboData.Projection);
		}
		mOverrideMaterial.reset();
		mActiveStage = nullptr;
		if (stage->mMaterial) {
			stage->mMaterial->unbind();
		}
		render.setFrameBuffer(nullptr);
		stage->mExecuted = true;
		if (stage->mName == "CloudShadowMap") {
			mCloudShadowNeedsUpdate = false;
		}
		if (stage->mDescriptor.Once) {
			std::cout << "Pipeline init: " << stage->mName << std::endl;
		}
		else if (stage->mDescriptor.UpdateInterval > 1 && stage->mName != "CloudShadowMap") {
			std::cout << "Pipeline update: " << stage->mName << std::endl;
		}
	};
	auto submitCpuProfile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("GameRenderer::submitStages") : nullptr;
	if (!mRenderPipeline->mIsInitialized) {
		for (auto& stage : mRenderPipeline->mStartStages) {
			runStage(stage.get());
		}
		mRenderPipeline->mIsInitialized = true;
	}
	const bool asyncCompute = mRenderPipeline->mDescriptor.Execution.AsyncCompute && presentationDriver->hasGraphComputeQueue();
	bool computePending = false;
	for (const auto& pass : mRenderPipeline->mGraphPlan.Schedule) {
		if (asyncCompute && pass.WaitForCompute) {
			mContext.render->execute([presentationDriver] { presentationDriver->joinGraphCompute(); });
			computePending = false;
		}
		if (auto activations = mRenderPipeline->mAliasActivations.find(pass.Id); activations != mRenderPipeline->mAliasActivations.end()) {
			mContext.render->execute([presentationDriver, textures = activations->second] {
				for (const auto& texture : textures) {
					presentationDriver->activateAliasedRenderTarget(*texture);
				}
			});
		}
		if (!pass.Barriers.empty()) {
			const bool uav = std::any_of(pass.Barriers.begin(), pass.Barriers.end(), [](const auto& barrier) { return barrier.Uav; });
			mContext.render->execute([presentationDriver, uav] { presentationDriver->renderGraphBarrier(uav); });
		}
		auto* stage = mRenderPipeline->mStageIndex[pass.Id];
		if (asyncCompute && pass.ExecutionQueue == GRAPH::Queue::ASYNC_COMPUTE) {
			runComputeStage(*stage, true);
			computePending = true;
		}
		else {
			runStage(stage);
		}
	}
	if (computePending) {
		mContext.render->execute([presentationDriver] { presentationDriver->joinGraphCompute(); });
	}
	mVirtualGeometryShadows.endFrame();
	if (mVirtualGeometry) {
		mVirtualGeometry->endFrame();
	}
	submitCpuProfile.reset();
	auto& screenshot = CAPTURE::ScreenshotService::Get();
	if (screenshot.pending() && !screenshot.textureName().empty()) {
		const auto& name = screenshot.textureName();
		const auto& descriptor = mRenderPipeline->mDescriptor;
		const auto source = mRenderPipeline->mTextures.find(name);
		if (source == mRenderPipeline->mTextures.end()) {
			screenshot.fail("Unknown screenshot texture: " + name);
		}
		else if (descriptor.Graph.Enabled
			&& std::find(descriptor.Graph.Exports.begin(), descriptor.Graph.Exports.end(), name) == descriptor.Graph.Exports.end()
			&& std::none_of(descriptor.Textures.begin(), descriptor.Textures.end(),
				[&](const auto& texture) { return texture.Name == name && texture.History; })) {
			screenshot.fail("Screenshot texture is not exported by the render graph: " + name);
		}
		else {
			screenshot.setSource(source->second);
		}
	}
	mRenderPipeline->advanceHistory(*mContext.render);
	if (rayTracingActive) {
		++mRaySample;
	}
	mPreviousViewProjection = viewProjection;
	mPreviousJitter = mJitter;
	mPreviousUnjitteredViewProjection = unjitteredViewProjection;
	mPreviousCamera = &cameraComponent;
	mRenderPipeline->mHistory.markValid(historyEffectBit(HistoryEffect::Presentation));
	if (mFrameCount % 60 == 59) {
		logHistoryCounters(false);
	}
}

void GameRenderer::captureShadows(PipelineStage& active, const std::vector<RenderChunk>& shadowChunks, const MATH::Vector3f& cameraPosition,
	const MATH::Vector2f& size, GpuScene* gpuScene) {
	auto* stage = &active;
	auto& render = *mContext.render;

	if (!stage->mMaterial || !stage->mFrameBuffer) {
		throw std::runtime_error("Shadow stage requires material and framebuffer");
	}
#if defined(IKIGAI_USE_GPU_DRIVEN)
	if (gpuScene) {
		if (!mOverrideMaterial
			|| !SupportsGpuSceneInstancing(
				*stage->mMaterial, "/shaders/render/portable/shadow.shader", "/shaders/render/portable/pointShadowCube.shader")) {
			gpuScene = nullptr;
		}
	}
#endif
	const auto& type = stage->mDescriptor.ShadowType;
	const int lightType = type == "directional" ? 1 : type == "spot" ? 2 : (type == "point" || type == "pointCube") ? 0 : -2;
	int index = mCaptureLightIndex;
	for (size_t i = 0; index < 0 && i < mLights.size(); ++i) {
		if (mLights[i].type == lightType
			&& (stage->mDescriptor.ShadowLightId < 0 || (i < mLightObjectIds.size() && mLightObjectIds[i] == stage->mDescriptor.ShadowLightId))) {
			index = static_cast<int>(i);
			break;
		}
	}
	if (index >= 0) {
		const auto& light = mLights[index];
		MATH::Vector3f position(light.pos[0], light.pos[1], light.pos[2]);
		auto direction = MATH::Vector3f::Normalize(MATH::Vector3f(light.forward[0], light.forward[1], light.forward[2]));
		const bool cube = type == "pointCube";
		const float nearPlane = mCaptureLightIndex >= 0 && light.shadowNear > 0 ? light.shadowNear
			: cube																? std::get<float>(stage->mMaterial->get("data_UBONear"))
																				: .1f;
		const float range = mCaptureLightIndex >= 0 && light.shadowFar > 0 ? light.shadowFar
			: cube														   ? std::get<float>(stage->mMaterial->get("data_UBOFar"))
																		   : std::max(stage->mDescriptor.ShadowRange, 1.0f);
		if (!std::isfinite(nearPlane) || !std::isfinite(range) || nearPlane <= 0 || range <= nearPlane) {
			throw std::runtime_error("Invalid point shadow near/far");
		}
		if (cube) {
			mShadowData.Settings.y = nearPlane;
		}
		const int faces = lightType == 1 ? 4 : lightType == 0 ? 6 : 1;
		const int columns = lightType == 1 ? 2 : lightType == 0 ? 3 : 1, rows = faces / columns;
		const int base = lightType == 1 ? 0 : lightType == 2 ? 4 : 5;
		if (lightType == 1) {
			mShadowData.LightIndices.x = static_cast<float>(index);
			mShadowData.CascadeSplits = { range * .125f, range * .25f, range * .5f, range };
		}
		if (lightType == 2) {
			mShadowData.LightIndices.y = static_cast<float>(index);
		}
		if (lightType == 0) {
			mShadowData.LightIndices.z = static_cast<float>(index);
			mShadowData.PointPositionRange = { position.x, position.y, position.z, range };
		}
		const MATH::Vector3f directions[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		const MATH::Vector3f ups[6] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
		for (int face = cube ? stage->mDescriptor.ShadowFace : 0; face < (cube ? stage->mDescriptor.ShadowFace + 1 : faces); ++face) {
			MATH::Matrix4f view, proj;
			if (lightType == 1) {
				const float radius = range * std::pow(2.0f, static_cast<float>(face) - 3.0f);
				const auto eye = cameraPosition - direction * range;
				const auto up = std::abs(direction.y) > .95f ? MATH::Vector3f(0, 0, 1) : MATH::Vector3f(0, 1, 0);
				view = MATH::Matrix4f::CreateView(eye, cameraPosition, up);
				proj = MATH::Matrix4f::CreateOrthographic(-radius, radius, -radius, radius, .1f, range * 2);
			}
			else {
				const auto dir = lightType == 0 ? directions[face] : direction;
				const auto up = lightType == 0 ? ups[face] : (std::abs(dir.y) > .95f ? MATH::Vector3f(0, 0, 1) : MATH::Vector3f(0, 1, 0));
				view = MATH::Matrix4f::CreateView(position, position + dir, up);
				const float fov = lightType == 0 ? 90.0f : std::clamp(light.outerCutoff * 2, 1.0f, 175.0f);
				// Near is a radial cutoff, while projection clips axially.
				proj = MATH::Matrix4f::CreatePerspective(fov, 1.0f, cube ? nearPlane / std::sqrt(3.0f) : nearPlane, range);
			}
#if defined(IKIGAI_USE_GPU_DRIVEN)
			bool gpuCasters = false;
			if (gpuScene) {
				Frustum frustum;
				frustum.make(proj * view);
				gpuCasters = gpuScene->recordShadow(render, frustum);
			}
#endif
			Frustum vgFrustum;
			const bool vgCasters = lightType == 1 && stage->mVgShadowShader && mVirtualGeometry && mVirtualGeometry->settings().Enabled;
			if (vgCasters) {
				vgFrustum.make(proj * view);
			}
			if (DriverInterface::settings.backend != RenderSettings::Backend::OPENGL) {
				auto correction = MATH::Matrix4f::Identity;
				correction(2, 2) = .5f;
				correction(2, 3) = .5f;
				proj = correction * proj;
			}
			// Cube addressing has the same -Y face rule on all APIs. Explicit APIs
			// reverse the viewport Y convention; compensate only capture projection.
			if (cube && DriverInterface::settings.backend != RenderSettings::Backend::OPENGL) {
				auto flip = MATH::Matrix4f::Identity;
				flip(1, 1) = -1;
				proj = flip * proj;
			}
			const auto vp = MATH::Matrix4f::Transpose(proj * view);
			mShadowData.Matrices[base + face] = vp;
			mShadowPassUbo = render.createUniformBuffer<MATH::Matrix4f>(vp);
			if (cube) {
				mPointShadowPassUbo = render.createUniformBuffer<PointShadowPassUBO>(
					{ vp, { position.x, position.y, position.z, range }, { nearPlane, range, 0, 0 } });
				render.setViewport({ { 0, 0 }, size });
			}
			else {
				render.setViewport(
					{ { size.x / columns * (face % columns), size.y / rows * (face / columns) }, { size.x / columns, size.y / rows } });
			}
			if (vgCasters) {
				VG::VirtualGeometryView shadowView;
				shadowView.Frustum = &vgFrustum;
				shadowView.Orthographic = true;
				shadowView.ProjectionScale = size.y / rows * proj(1, 1) * 0.5f;
				const Depth depth { stage->mMaterial->isDepthTest() ? stage->mMaterial->getDepthFunc() : DepthFunction::ALWAYS,
					stage->mMaterial->isDepthWriting() };
				mVirtualGeometryShadows.record(
					render, shadowView, stage->mDescriptor.VirtualGeometryShadows, stage->mVgShadowShader, mShadowPassUbo, depth);
			}
			const auto drawCaster = [&](const Drawable& drawable) {
				if (drawable.material && drawable.material->isCastShadow()) {
					if (vgCasters && mVirtualGeometryShadows.replaces(drawable)) {
						return;
					}
#if defined(IKIGAI_USE_GPU_DRIVEN)
					if (gpuCasters && gpuScene->containsShadow(drawable)) {
						return;
					}
#endif
					drawDrawable(drawable);
				}
			};
			for (const auto& chunk : shadowChunks) {
				for (const auto& [distance, drawable] : chunk.opaqueDrawablesForward) {
					drawCaster(drawable);
				}
				for (const auto& [distance, drawable] : chunk.opaqueDrawablesDeferred) {
					drawCaster(drawable);
				}
			}
#if defined(IKIGAI_USE_GPU_DRIVEN)
			if (gpuCasters) {
				drawGpuScene(true);
			}
#endif
		}
	}
	mContext.render->updateUniformBuffer(mShadowUbo, mShadowData);
}

namespace {
#if !defined(IKIGAI_GL_HAS_SSBO)
	void PadLightsForEsUbo(std::vector<LightOGL>& lights) {
		if (lights.size() > MAX_LIGHTS) {
			lights.resize(MAX_LIGHTS);
		}
		else {
			lights.resize(MAX_LIGHTS, LightOGL::Inactive());
		}
	}
#endif
}

void GameRenderer::updateLights(SCENE_SYSTEM::Scene& scene) {
	auto lightMatrices = scene.findLightData(&mLightObjectIds);
	if (lightMatrices.empty()) {
		lightMatrices.push_back(LightOGL::Inactive());
	}
#if !defined(IKIGAI_GL_HAS_SSBO)
	PadLightsForEsUbo(lightMatrices);
#endif
	mContext.render->updateStorageBuffer(mLightSSBO, lightMatrices);
	mLights = std::move(lightMatrices);
}

void GameRenderer::updateLightsInFrustum(SCENE_SYSTEM::Scene& scene, const Frustum& frustum) {
	auto lightMatrices = scene.findLightDataInFrustum(frustum, &mLightObjectIds);
	if (lightMatrices.empty()) {
		lightMatrices.push_back(LightOGL::Inactive());
	}
#if !defined(IKIGAI_GL_HAS_SSBO)
	PadLightsForEsUbo(lightMatrices);
#endif
	mContext.render->updateStorageBuffer(mLightSSBO, lightMatrices);
	mLights = std::move(lightMatrices);
}

const RenderGraphPipeline& GameRenderer::getCurrentPipeline() const {
	if (!mRenderPipeline) {
		throw std::runtime_error("Render pipeline is not ready");
	}
	return *mRenderPipeline;
}

void GameRenderer::setPipeline(std::unique_ptr<RenderGraphPipeline>&& renderPipeline) {
	mRayScene.reset();
	mRayTopLevels.clear();
	mRayTriangles.reset();
	mRayInstances.reset();
	mRayLightSSBO.reset();
	mRayLights.clear();
	mRayTopologyHash = 0;
	mRayMaterialHash = 0;
	mRayDynamicHash = 0;
	mRaySample = 0;
	mSkinningPoses.clear();
	mRayAlbedoMaps.clear();
	mRayWhite.reset();
	mRayFrameUbo.reset();
	mRaySceneChanged = true;
	if (mRenderPipeline) {
		mRetiredHistoryCounters.add(mRenderPipeline->mHistory.counters());
	}
	if (renderPipeline) {
		renderPipeline->mHistory.invalidate(HistoryReason::PipelineReload);
	}
	mRenderPipeline = std::move(renderPipeline);
#if defined(IKIGAI_USE_GPU_DRIVEN)
	mGpuSceneStage = nullptr;
	mGpuScene.reset();
	if (mRenderPipeline->mDescriptor.Execution.DrivenMode.value_or(SubmissionMode::CPU) != SubmissionMode::CPU) {
		const auto geometry = std::ranges::find_if(mRenderPipeline->mStages, [](const auto& stage) {
			return stage->mDrawContent == DrawContent::DEFERRED && stage->mName == "Geometry" && stage->mDescriptor.OverrideMaterial
				&& stage->mMaterial && SupportsGpuSceneInstancing(*stage->mMaterial, OrdinaryGeometryShader, OrdinaryEmissiveGeometryShader);
		});
		if (geometry != mRenderPipeline->mStages.end()) {
			mGpuSceneStage = geometry->get();
		}
		else {
			std::cout << "GPU_SCENE_FALLBACK requires the portable Geometry pass\n";
		}
	}
#else
	if (mRenderPipeline->mDescriptor.Execution.DrivenMode == SubmissionMode::GPU) {
		std::cout << "GPU_SCENE_FALLBACK ordinary GPU submission is disabled in this build\n";
	}
#endif
	mCloudShadowAnchorValid = false;
	mCloudShadowNeedsUpdate = true;
	mAtmosphereCache = {};
	mAtmosphereTransmittanceStage = nullptr;
	mAtmosphereSkyViewStage = nullptr;
	mAtmosphereAerialStage = nullptr;
	for (auto& stage : mRenderPipeline->mStartStages) {
		if (stage->mName == "AtmosphereTransmittance") {
			mAtmosphereTransmittanceStage = stage.get();
		}
		else if (stage->mName == "AtmosphereSkyView") {
			mAtmosphereSkyViewStage = stage.get();
		}
		else if (stage->mName == "AtmosphereAerial") {
			mAtmosphereAerialStage = stage.get();
		}
	}
	mPbrPassMaterials.clear();
}

#ifdef OCULUS
void GameRenderer::renderSceneOculus(XrCompositionLayerProjectionView& layerView, render_target_t& rtarget, XrPosef& stagePose, uint32_t viewID) {
	(void)stagePose;

	if (mContext.sceneManager->isTransitioning() || !mContext.sceneManager->hasCurrentScene()) {
		const auto& rect = layerView.subImage.imageRect;
		glBindFramebuffer(GL_FRAMEBUFFER, rtarget.fbo_id);
		glViewport(rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (auto* loading = mContext.sceneManager->getPresentedScene()) {
			uboData.ViewportSize = { float(rect.extent.width), float(rect.extent.height) };
			uboData.Time = static_cast<float>(TIME::Timer::GetInstance().getTimeSinceStart().count());
			mContext.render->updateUniformBuffer(mEngineUbo, uboData);
			renderGui(*loading);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return;
	}

	if (!mContext.sceneManager->hasCurrentScene()) {
		return;
	}
	auto& scene = mContext.sceneManager->getCurrentScene();
	auto mainCamera = scene.findMainCamera();
	if (!mainCamera) {
		return;
	}

	ECS::CameraComponent* cameraComp = mainCamera.get();
	if (auto* vrCamera = dynamic_cast<ECS::VrCameraComponent*>(cameraComp)) {
		auto eyeObject = (viewID == 0) ? vrCamera->left : vrCamera->right;
		if (eyeObject) {
			if (auto eyeCamera = eyeObject->getComponent<ECS::CameraComponent>()) {
				cameraComp = eyeCamera.get();
			}
		}
	}

	auto& window = *mContext.window;
	const int view_x = layerView.subImage.imageRect.offset.x;
	const int view_y = layerView.subImage.imageRect.offset.y;
	const int view_w = layerView.subImage.imageRect.extent.width;
	const int view_h = layerView.subImage.imageRect.extent.height;
	window.setSize(static_cast<unsigned int>(view_w), static_cast<unsigned int>(view_h));

	glBindFramebuffer(GL_FRAMEBUFFER, rtarget.fbo_id);
	glViewport(view_x, view_y, view_w, view_h);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	XrMatrix4x4f matP {};
	XrMatrix4x4f matV {};
	XrMatrix4x4f matC {};
	XrMatrix4x4f_CreateProjectionFov(&matP, GRAPHICS_OPENGL_ES, layerView.fov, cameraComp->getNear(), cameraComp->getFar());

	const XrVector3f scale = { 1.0f, 1.0f, 1.0f };
	XrMatrix4x4f_CreateTranslationRotationScale(&matC, &layerView.pose.position, &layerView.pose.orientation, &scale);
	XrMatrix4x4f_InvertRigidBody(&matV, &matC);

	auto toMat4 = [](const XrMatrix4x4f& from) {
		MATH::Matrix4f to(from.m[0], from.m[1], from.m[2], from.m[3], from.m[4], from.m[5], from.m[6], from.m[7], from.m[8], from.m[9], from.m[10],
			from.m[11], from.m[12], from.m[13], from.m[14], from.m[15]);
		return MATH::Matrix4f::Transpose(to);
	};
	cameraComp->getCamera().cacheViewMatrix(toMat4(matV));
	cameraComp->getCamera().cacheProjectionMatrix(toMat4(matP));

	renderScene(scene, *cameraComp);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
#endif
} // namespace IKIGAI::RENDER

namespace IKIGAI::RENDER {
// Validate resolved capture settings before a reload can mutate stages/resources.
static void ValidatePointCube(const RenderGraphPipeline::Descriptor& desc) {
	for (const auto& stage : desc.StartStages) {
		if (stage.ShadowType == "pointCube") {
			throw std::runtime_error("Point cube capture must run every frame, not in StartStages");
		}
	}
	std::map<std::string, std::set<int>> faces;
	std::map<std::string, std::tuple<int, float, float>> contracts;
	for (const auto* stages : { &desc.StartStages, &desc.Stages }) {
		for (const auto& stage : *stages) {
			if (stage.ShadowType != "pointCube") {
				continue;
			}
			auto fail = [&] { throw std::runtime_error("Invalid pointCube contract: " + stage.Name); };
			const auto frame
				= std::find_if(desc.FrameBuffers.begin(), desc.FrameBuffers.end(), [&](const auto& f) { return f.Name == stage.FrameBuffer; });
			if (stage.Draw != DrawContent::SHADOW || stage.Once || stage.UpdateInterval != 1 || !stage.Enabled || !stage.ClearColor
				|| !stage.ClearDepth || stage.ClearValue.x != 1 || stage.ShadowFace < 0 || stage.ShadowFace > 5 || frame == desc.FrameBuffers.end()
				|| frame->Layer != stage.ShadowFace || frame->Mip != 0 || frame->Textures.size() != 1 || frame->Depth.empty()) {
				fail();
			}
			const auto color = std::find_if(desc.Textures.begin(), desc.Textures.end(), [&](const auto& t) { return t.Name == frame->Textures[0]; });
			const auto depth = std::find_if(desc.Textures.begin(), desc.Textures.end(), [&](const auto& t) { return t.Name == frame->Depth; });
			if (color == desc.Textures.end() || depth == desc.Textures.end()) {
				fail();
			}
			const auto& r = color->Resource;
			const auto& d = depth->Resource;
			if (!color->Path.empty() || !depth->Path.empty() || r.texType != TextureType::TEXTURE_CUBE || r.pixelType != PixelFormat::R_FLOAT
				|| r.width <= 0 || r.height != r.width || r.depth != 6 || r.mipMapCount != 1 || r.useMipmap || r.minFilter != MinMagFilter::NEAREST
				|| r.magFilter != MinMagFilter::NEAREST || d.texType != TextureType::DEPTH || d.width != r.width || d.height != r.height) {
				fail();
			}
			auto loaded = UTILS::FromJson<MaterialResource>(stage.Material);
			if (loaded.isErr()) {
				fail();
			}
			auto uniforms = loaded.unwrap().Uniforms;
			for (const auto& [name, value] : stage.Uniforms) {
				std::visit([&](const auto& v) { uniforms[name] = v; }, value);
			}
			auto scalar = [&](const std::string& name) {
				if (!uniforms.contains(name) || !std::holds_alternative<float>(uniforms.at(name))) {
					fail();
					return 0.f;
				}
				return std::get<float>(uniforms.at(name));
			};
			const float nearPlane = scalar("data_UBONear"), farPlane = scalar("data_UBOFar");
			if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane) {
				fail();
			}
			const auto contract = std::make_tuple(stage.ShadowLightId, nearPlane, farPlane);
			if (contracts.contains(color->Name) && contracts.at(color->Name) != contract) {
				fail();
			}
			contracts[color->Name] = contract;
			if (!faces[color->Name].insert(stage.ShadowFace).second) {
				fail();
			}
		}
	}
	if (faces.size() > 1) {
		throw std::runtime_error("Only one shadowed point cubemap is supported");
	}
	if (!faces.empty()) {
		for (const auto& stage : desc.Stages) {
			if (stage.ShadowType == "point") {
				throw std::runtime_error("Point atlas and cube captures cannot share one shadow light slot");
			}
		}
	}
	for (const auto& [name, written] : faces) {
		if (written.size() != 6) {
			throw std::runtime_error("Point cubemap requires six capture stages: " + name);
		}
	}
}
static RenderGraphPipeline::Descriptor ResolvePipelineSettings(const RenderGraphPipeline::Descriptor& descriptor) {
	auto resolved = descriptor;
	RenderGraphPipeline::SettingsDescriptor merged;
	auto apply = [&](RenderGraphPipeline::SettingsDescriptor settings) {
		for (const auto& group : settings.Groups) {
			for (const auto& path : group.Materials) {
				for (const auto& [name, value] : group.Uniforms) {
					const auto alias = group.Rename.find(path);
					const auto key = alias != group.Rename.end() && alias->second.contains(name) ? alias->second.at(name) : name;
					settings.Materials[path][key] = value;
				}
			}
		}
		for (const auto& [path, uniforms] : settings.Materials) {
			for (const auto& [name, value] : uniforms) {
				merged.Materials[path][name] = value;
			}
		}
		for (const auto& replacement : settings.Textures) {
			auto target = std::find_if(resolved.Textures.begin(), resolved.Textures.end(), [&](const auto& t) { return t.Name == replacement.Name; });
			if (target == resolved.Textures.end()) {
				throw std::runtime_error("Pipeline settings reference unknown texture: " + replacement.Name);
			}
			*target = replacement;
		}
	};
	apply(descriptor.Settings);
	if (!descriptor.SettingsAsset.empty()) {
		auto result = UTILS::FromJson<RenderGraphPipeline::SettingsDescriptor>(descriptor.SettingsAsset);
		if (result.isErr()) {
			throw std::runtime_error("Pipeline settings asset: " + result.unwrapErr().text);
		}
		apply(result.unwrap());
	}
	std::vector<std::vector<PipelineStage::Descriptor>*> groups = { &resolved.StartStages, &resolved.Stages };
	for (auto& pass : resolved.LightPasses) {
		groups.push_back(&pass.InitStages);
		groups.push_back(&pass.CaptureStages);
		groups.push_back(&pass.PrepareStages);
		groups.push_back(&pass.ResolveStages);
	}
	for (auto* stages : groups) {
		for (auto& stage : *stages) {
			if (!merged.Materials.contains(stage.Material)) {
				continue;
			}
			auto uniforms = merged.Materials.at(stage.Material);
			// Explicit stage values (face, roughness, quality) take precedence.
			for (const auto& [name, value] : stage.Uniforms) {
				uniforms[name] = value;
			}
			stage.Uniforms = std::move(uniforms);
		}
	}
	ResolveScreenProbes(resolved);
	ResolveTemporalDenoisers(resolved);
	auto validation = resolved;
	for (const auto& pass : resolved.LightPasses) {
		if (pass.ShadowBudget < -1 || pass.VolumeLightLimit < -1) {
			throw std::runtime_error("Invalid light pass budget");
		}
		for (const auto* children : { &pass.InitStages, &pass.CaptureStages, &pass.PrepareStages, &pass.ResolveStages }) {
			for (const auto& child : *children) {
				if (child.Draw == DrawContent::LIGHT_ACCUMULATION || child.Once || child.UpdateInterval != 1 || !child.Enabled) {
					throw std::runtime_error("Invalid light pass child: " + child.Name);
				}
			}
		}
		validation.Stages.insert(validation.Stages.end(), pass.CaptureStages.begin(), pass.CaptureStages.end());
		int point = 0, spot = 0, directional = 0;
		for (const auto& capture : pass.CaptureStages) {
			if (capture.Draw != DrawContent::SHADOW || capture.ShadowLightId >= 0 || !capture.OverrideMaterial) {
				throw std::runtime_error("Managed capture must use the current light: " + capture.Name);
			}
			if (capture.ShadowType == "pointCube") {
				++point;
			}
			else if (capture.ShadowType == "spot") {
				++spot;
			}
			else if (capture.ShadowType == "directional") {
				++directional;
			}
			else {
				throw std::runtime_error("Unsupported managed shadow type: " + capture.Name);
			}
			if (capture.ShadowType != "pointCube" && (!std::isfinite(capture.ShadowRange) || capture.ShadowRange <= .1f)) {
				throw std::runtime_error("Invalid projected shadow range: " + capture.Name);
			}
		}
		if (point != 6 || spot != 1 || directional != 1) {
			throw std::runtime_error("Managed lights require six point faces, spot and directional capture");
		}
		for (const auto& resolve : pass.ResolveStages) {
			if (resolve.Draw != DrawContent::QUAD || !resolve.Additive || resolve.ClearColor || resolve.ClearDepth) {
				throw std::runtime_error("Per-light resolve must accumulate without clearing: " + resolve.Name);
			}
		}
	}
	for (const auto& stage : resolved.Stages) {
		if (stage.Draw == DrawContent::LIGHT_ACCUMULATION
			&& std::count_if(resolved.LightPasses.begin(), resolved.LightPasses.end(), [&](const auto& p) { return p.Name == stage.LightPass; })
				!= 1) {
			throw std::runtime_error("Unknown light pass: " + stage.LightPass);
		}
	}
	ValidatePointCube(validation);
	auto rejectFG = [](const auto& stages) {
		for (const auto& stage : stages) {
			if (stage.Draw == DrawContent::FRAME_GENERATION) {
				throw std::runtime_error("FRAME_GENERATION is allowed only in the main per-frame stage list");
			}
		}
	};
	rejectFG(resolved.StartStages);
	for (const auto& pass : resolved.LightPasses) {
		rejectFG(pass.InitStages);
		rejectFG(pass.CaptureStages);
		rejectFG(pass.PrepareStages);
		rejectFG(pass.ResolveStages);
	}
	std::set<std::string> produced, fgInputs;
	unsigned fgCount = 0;
	for (const auto& stage : resolved.Stages) {
		if (!stage.Enabled) {
			continue;
		}
		std::set<std::string> writes(stage.Writes.begin(), stage.Writes.end());
		if (!stage.DispatchTexture.empty()) {
			writes.insert(stage.DispatchTexture);
		}
		if (!stage.FrameBuffer.empty()) {
			const auto frame = std::find_if(
				resolved.FrameBuffers.begin(), resolved.FrameBuffers.end(), [&](const auto& f) { return f.Name == stage.FrameBuffer; });
			if (frame != resolved.FrameBuffers.end()) {
				writes.insert(frame->Textures.begin(), frame->Textures.end());
				if (!frame->Depth.empty()) {
					writes.insert(frame->Depth);
				}
			}
		}
		for (const auto& output : writes) {
			if (fgInputs.contains(output)) {
				throw std::runtime_error("FG input is overwritten after FRAME_GENERATION: " + output);
			}
		}
		if (stage.Draw == DrawContent::FRAME_GENERATION) {
			if (++fgCount > 1) {
				throw std::runtime_error("Only one main FRAME_GENERATION stage is allowed");
			}
			if (!writes.empty() || !stage.Material.empty() || !stage.Program.empty()) {
				throw std::runtime_error("FRAME_GENERATION must not write a framebuffer or use a shader material");
			}
			for (const char* key : { "Color", "Depth", "MotionVectors", "UI" }) {
				if (!stage.Bindings.contains(key)) {
					throw std::runtime_error("Missing FG input: " + std::string(key));
				}
				const auto& name = stage.Bindings.at(key);
				const auto resource = std::find_if(resolved.Textures.begin(), resolved.Textures.end(), [&](const auto& t) { return t.Name == name; });
				const bool scene = std::string_view(key) == "Color" || std::string_view(key) == "UI";
				const auto format = scene			   ? PixelFormat::RGBA_INT
					: std::string_view(key) == "Depth" ? PixelFormat::DEPTH32_FLOAT
													   : PixelFormat::RG_FLOAT;
				const auto size = scene ? RenderGraphPipeline::TextureDesc::SizeBase::DISPLAY : RenderGraphPipeline::TextureDesc::SizeBase::RENDER;
				if (resource == resolved.Textures.end() || resource->Resource.pixelType != format || resource->Size != size || resource->Scale != 1
					|| resource->Resource.width || resource->Resource.height) {
					throw std::runtime_error("Invalid FG texture format/size: " + name);
				}
				if (!produced.contains(name)) {
					throw std::runtime_error("FG texture must be produced each frame before the FG stage: " + name);
				}
				fgInputs.insert(name);
			}
		}
		if (!stage.Once && stage.UpdateInterval == 1) {
			produced.insert(writes.begin(), writes.end());
		}
	}
	ValidateGeometryOcclusionPostOrder(resolved.Stages);
	return resolved;
}

static void ValidateGraphMaterial(const PipelineStage& stage, const PipelineStage::Descriptor& descriptor) {
	if (!stage.mMaterial) {
		return;
	}
	using Access = GRAPH::Access;
	using UniformType = ShaderReflection::UniformType;
	const auto& reflection = stage.mMaterial->getShader()->getReflection();
	const auto require = [&](const std::string& binding, const std::string& resource, bool bufferBinding) {
		const auto id = reflection.mNameToUniforms.identifier(binding);
		if (!reflection.mNameToUniforms.contains(id)) {
			throw std::invalid_argument(descriptor.Name + " has no shader binding " + binding);
		}
		const auto& uniform = reflection.mUniforms.at(reflection.mNameToUniforms.at(id));
		if (bufferBinding != (uniform.mType == UniformType::UNIFORM_BUFFER || uniform.mType == UniformType::STORAGE_BUFFER)) {
			throw std::invalid_argument(descriptor.Name + " uses the wrong binding kind for " + binding);
		}
		Access access;
		switch (uniform.mType) {
		case UniformType::SAMPLER_2D:
		case UniformType::SAMPLER_3D:
		case UniformType::SAMPLER_CUBE:
		case UniformType::SAMPLER_2D_ARRAY:
			access = Access::SAMPLED;
			break;
		case UniformType::UNIFORM_BUFFER:
			access = Access::UNIFORM;
			break;
		case UniformType::IMAGE_2D:
		case UniformType::IMAGE_3D:
		case UniformType::IMAGE_2D_ARRAY:
		case UniformType::IMAGE_CUBE:
		case UniformType::STORAGE_BUFFER:
			access = uniform.mReadOnly ? Access::STORAGE_READ : Access::STORAGE_WRITE;
			break;
		default:
			throw std::invalid_argument(descriptor.Name + " has an unsupported graph binding " + binding);
		}
		if (std::none_of(descriptor.ResourceUses.begin(), descriptor.ResourceUses.end(),
				[&](const auto& use) { return use.Resource == resource && use.Mode == access; })) {
			throw std::invalid_argument(descriptor.Name + " graph access disagrees with shader binding " + binding);
		}
	};
	for (const auto& [binding, value] : descriptor.Uniforms) {
		if (const auto* resource = std::get_if<std::string>(&value)) {
			require(binding, *resource, false);
		}
	}
	for (const auto& [binding, resource] : descriptor.BufferLinks) {
		if (resource != "Lights") {
			require(binding, resource, true);
		}
	}
}

static PipelineStage::ResolvedBindings ResolveStageBindings(
	const RenderGraphPipeline& pipeline, const PipelineStage& stage, const PipelineStage::Descriptor& descriptor) {
	PipelineStage::ResolvedBindings result;
	if (!descriptor.Enabled || !stage.mMaterial || (stage.mCompute && stage.mCompute->Bypassed)) {
		return result;
	}
	result.Shader = stage.mMaterial->getShader();
	result.Generation = result.Shader->generation();
	const auto& reflection = result.Shader->getReflection();
	using UniformType = ShaderReflection::UniformType;
	const auto findUniform = [&](const std::string& name) -> const ShaderReflection::Uniform& {
		const auto id = reflection.mNameToUniforms.identifier(name);
		if (!reflection.mNameToUniforms.contains(id)) {
			throw std::invalid_argument(descriptor.Name + ": unknown shader binding " + name);
		}
		return reflection.mUniforms.at(reflection.mNameToUniforms.at(id));
	};
	for (const auto& [name, value] : descriptor.Uniforms) {
		if (const auto* texture = std::get_if<std::string>(&value)) {
			const auto& uniform = findUniform(name);
			switch (uniform.mType) {
			case UniformType::SAMPLER_2D:
			case UniformType::SAMPLER_3D:
			case UniformType::SAMPLER_CUBE:
			case UniformType::SAMPLER_2D_ARRAY:
			case UniformType::IMAGE_2D:
			case UniformType::IMAGE_3D:
			case UniformType::IMAGE_CUBE:
			case UniformType::IMAGE_2D_ARRAY:
				break;
			default:
				throw std::invalid_argument(descriptor.Name + ": texture requires a sampler or image binding " + name);
			}
			const auto slot = pipeline.mTextures.find(*texture);
			if (slot == pipeline.mTextures.end()) {
				throw std::invalid_argument(descriptor.Name + ": unknown texture resource " + *texture);
			}
			result.Textures.push_back({ uniform.mBind, &slot->second });
		}
		else {
			auto key = name;
			std::erase(key, '.');
			std::visit(
				[&](const auto& constant) {
					using T = std::decay_t<decltype(constant)>;
					if constexpr (!std::is_same_v<T, std::string>) {
						result.Constants.emplace_back(key, constant);
					}
				},
				value);
		}
	}
	for (const auto& [name, buffer] : descriptor.BufferLinks) {
		const auto& uniform = findUniform(name);
		if (uniform.mType == UniformType::UNIFORM_BUFFER) {
			const auto slot = pipeline.mUniformBuffers.find(buffer);
			if (slot == pipeline.mUniformBuffers.end()) {
				throw std::invalid_argument(descriptor.Name + ": unknown uniform buffer " + buffer);
			}
			result.UniformBuffers.push_back({ uniform.mBind, &slot->second });
		}
		else if (uniform.mType == UniformType::STORAGE_BUFFER) {
			if (buffer == "Lights") {
				result.Lights.push_back(uniform.mBind);
			}
			else {
				const auto slot = pipeline.mStorageBuffers.find(buffer);
				if (slot == pipeline.mStorageBuffers.end()) {
					throw std::invalid_argument(descriptor.Name + ": unknown storage buffer " + buffer);
				}
				result.StorageBuffers.push_back({ uniform.mBind, &slot->second });
			}
		}
		else {
			throw std::invalid_argument(descriptor.Name + ": BufferLinks requires a buffer binding " + name);
		}
	}
	for (const auto& [bufferName, members] : descriptor.BufferOverrides) {
		const auto& uniform = findUniform(bufferName);
		if (uniform.mType != UniformType::UNIFORM_BUFFER) {
			throw std::invalid_argument(descriptor.Name + ": BufferOverrides requires a uniform buffer " + bufferName);
		}
		auto& resolved = result.Overrides.emplace_back();
		resolved.Binding = uniform.mBind;
		resolved.Bytes.resize(uniform.mSize);
		for (const auto& [name, value] : members) {
			const auto member
				= std::find_if(uniform.mMembers.begin(), uniform.mMembers.end(), [&](const auto& field) { return field.mName == name; });
			if (member == uniform.mMembers.end() || member->mArraySize > 1) {
				throw std::invalid_argument(descriptor.Name + ": invalid buffer member " + name);
			}
			std::visit(
				[&](const auto& constant) {
					using T = std::decay_t<decltype(constant)>;
					if constexpr (std::is_same_v<T, std::string>) {
						throw std::invalid_argument("Textures cannot be buffer members");
					}
					else {
						using Packed = std::conditional_t<std::is_same_v<T, bool>, int32_t, T>;
						if (member->mOffset < 0 || member->mSize < sizeof(Packed)
							|| static_cast<size_t>(member->mOffset) + sizeof(Packed) > resolved.Bytes.size()) {
							throw std::invalid_argument(descriptor.Name + ": buffer member size mismatch " + name);
						}
						const auto packed = static_cast<Packed>(constant);
						std::memcpy(resolved.Bytes.data() + member->mOffset, &packed, sizeof(packed));
					}
				},
				value);
		}
	}
	return result;
}

namespace {
	struct VgVisibilityPair {
		std::string Ids;
		bool CompatibleGBuffer = false;
	};

	VgVisibilityPair ConfigureVgVisibility(RenderGraphPipeline::Descriptor& pipeline, bool canAllocate) {
		using Stage = PipelineStage::Descriptor;
		const auto isVisibility = [](const Stage& stage) { return stage.Draw == DrawContent::VIRTUAL_GEOMETRY_VISIBILITY; };
		const auto isResolve = [](const Stage& stage) { return stage.Draw == DrawContent::VIRTUAL_GEOMETRY_RESOLVE; };
		const auto hasVisibilityStages = [&](const std::vector<Stage>& stages) {
			return std::ranges::any_of(stages, [&](const Stage& stage) { return isVisibility(stage) || isResolve(stage); });
		};
		if (hasVisibilityStages(pipeline.StartStages) || std::ranges::any_of(pipeline.LightPasses, [&](const auto& pass) {
				return hasVisibilityStages(pass.InitStages) || hasVisibilityStages(pass.CaptureStages) || hasVisibilityStages(pass.PrepareStages)
					|| hasVisibilityStages(pass.ResolveStages);
			})) {
			throw std::invalid_argument("Virtual geometry visibility stages must be per-frame main pipeline stages");
		}
		const auto visibility = std::ranges::find_if(pipeline.Stages, isVisibility);
		const auto resolve = std::ranges::find_if(pipeline.Stages, isResolve);
		if (visibility == pipeline.Stages.end() && resolve == pipeline.Stages.end()) {
			return {};
		}
		const auto invalid = [] { throw std::invalid_argument("Invalid virtual geometry visibility/resolve stage pair"); };
		const auto geometry = std::ranges::find_if(
			pipeline.Stages, [](const Stage& stage) { return stage.Name == "Geometry" && stage.Draw == DrawContent::DEFERRED; });
		if (std::ranges::count_if(pipeline.Stages, isVisibility) != 1 || std::ranges::count_if(pipeline.Stages, isResolve) != 1
			|| geometry == pipeline.Stages.end() || visibility != geometry + 1 || resolve != visibility + 1 || !geometry->Enabled || geometry->Once
			|| geometry->UpdateInterval != 1 || !geometry->ClearDepth || !visibility->Enabled || !resolve->Enabled || visibility->Once
			|| resolve->Once || visibility->UpdateInterval != 1 || resolve->UpdateInterval != 1 || visibility->ClearColor || visibility->ClearDepth
			|| resolve->ClearColor || resolve->ClearDepth || !visibility->Material.empty() || !resolve->Material.empty()
			|| !visibility->Program.empty() || !resolve->Program.empty() || std::ranges::any_of(pipeline.Stages, [](const Stage& stage) {
				   return stage.Enabled && (stage.Draw == DrawContent::VIRTUAL_GEOMETRY_POST || stage.Draw == DrawContent::GPU_SCENE_OCCLUSION_POST);
			   })) {
			invalid();
		}
		const auto frame = [&](const Stage& stage) {
			return std::ranges::find_if(pipeline.FrameBuffers, [&](const auto& descriptor) { return descriptor.Name == stage.FrameBuffer; });
		};
		const auto geometryFrame = frame(*geometry);
		const auto visibilityFrame = frame(*visibility);
		const auto resolveFrame = frame(*resolve);
		if (geometryFrame == pipeline.FrameBuffers.end() || visibilityFrame == pipeline.FrameBuffers.end()
			|| resolveFrame == pipeline.FrameBuffers.end() || visibilityFrame->Textures.size() != 1 || geometryFrame->Depth.empty()
			|| visibilityFrame->Depth != geometryFrame->Depth || !resolveFrame->Depth.empty() || resolveFrame->Textures != geometryFrame->Textures
			|| geometryFrame->Mip || visibilityFrame->Mip || resolveFrame->Mip || geometryFrame->Layer || visibilityFrame->Layer
			|| resolveFrame->Layer) {
			invalid();
		}
		const auto ids = visibilityFrame->Textures.front();
		const auto texture = std::ranges::find_if(pipeline.Textures, [&](const auto& value) { return value.Name == ids; });
		const auto depth = std::ranges::find_if(pipeline.Textures, [&](const auto& value) { return value.Name == geometryFrame->Depth; });
		const auto binding = resolve->Bindings.find("Visibility");
		if (binding == resolve->Bindings.end() || binding->second != ids || texture == pipeline.Textures.end() || depth == pipeline.Textures.end()
			|| texture->Resource.pixelType != PixelFormat::RG32_INT || texture->Resource.texType != TextureType::TEXTURE_2D
			|| texture->Resource.useMipmap || texture->FullMipChain || texture->Resource.minFilter != MinMagFilter::NEAREST
			|| texture->Resource.magFilter != MinMagFilter::NEAREST || texture->Size != depth->Size || texture->Scale != depth->Scale
			|| texture->Resource.width != depth->Resource.width || texture->Resource.height != depth->Resource.height || texture->History
			|| texture->Transient || depth->History || ids == geometryFrame->Depth
			|| std::ranges::find(geometryFrame->Textures, ids) != geometryFrame->Textures.end()
			|| std::ranges::count_if(pipeline.Textures, [&](const auto& value) { return value.Name == ids; }) != 1 || !texture->Path.empty()) {
			invalid();
		}
		const auto protectedTarget = [&](const std::string& name) {
			return name == ids || name == geometryFrame->Depth || std::ranges::find(geometryFrame->Textures, name) != geometryFrame->Textures.end();
		};
		for (auto stage = resolve + 1; stage != pipeline.Stages.end(); ++stage) {
			if (!stage->Enabled) {
				continue;
			}
			if (std::ranges::any_of(stage->Writes, protectedTarget) || std::ranges::any_of(stage->ResourceUses, [&](const auto& use) {
					return GRAPH::IsWrite(use.Mode) && protectedTarget(use.Resource);
				})) {
				invalid();
			}
			const auto laterFrame = frame(*stage);
			if (laterFrame == pipeline.FrameBuffers.end()) {
				continue;
			}
			const bool stockTransparent = stage->Draw == DrawContent::TRANSPARENT_GEOMETRY && stage->OverrideMaterial && !stage->ClearDepth
				&& stage->Material == "/materials/render/portable/forwardTransparent.mat";
			if (std::ranges::any_of(laterFrame->Textures, protectedTarget) || (laterFrame->Depth == geometryFrame->Depth && !stockTransparent)) {
				invalid();
			}
		}
		if (!canAllocate) {
			if (pipeline.Graph.Enabled) {
				throw std::invalid_argument("Explicit visibility graph requires RG32_INT attachment support");
			}
			const auto rasterName = visibility->FrameBuffer;
			const auto resolveName = resolve->FrameBuffer;
			std::erase_if(pipeline.Stages, [&](const Stage& stage) { return isVisibility(stage) || isResolve(stage); });
			std::erase_if(pipeline.FrameBuffers, [&](const auto& value) { return value.Name == rasterName || value.Name == resolveName; });
			std::erase_if(pipeline.Textures, [&](const auto& value) { return value.Name == ids; });
			return {};
		}
		return { ids, geometryFrame->Textures == std::vector<std::string> { "Velocity", "Normals", "Albedo", "Roughness" } };
	}

	void ValidateVgVisibilitySchedule(const RenderGraphPipeline::Descriptor& pipeline, const GRAPH::Plan& plan) {
		if (!pipeline.Graph.Enabled) {
			return;
		}
		const auto visibility
			= std::ranges::find_if(pipeline.Stages, [](const auto& stage) { return stage.Draw == DrawContent::VIRTUAL_GEOMETRY_VISIBILITY; });
		if (visibility == pipeline.Stages.end()) {
			return;
		}
		const auto id = static_cast<GRAPH::PassId>(visibility - pipeline.Stages.begin());
		const auto scheduled = std::ranges::find(plan.Schedule, id, &GRAPH::ScheduledPass::Id);
		if (scheduled == plan.Schedule.end() || scheduled == plan.Schedule.begin() || scheduled + 1 == plan.Schedule.end()
			|| (scheduled - 1)->Id != id - 1 || (scheduled + 1)->Id != id + 1) {
			throw std::invalid_argument("Visibility graph must schedule Geometry, visibility, and resolve consecutively");
		}
	}
}

RenderGraphPipeline::RenderGraphPipeline(const Descriptor& descriptor, Renderer& renderer, const std::string& pipelineAsset)
	: mDescriptor(ResolvePipelineSettings(descriptor))
	, mSourceDescriptor(descriptor)
	, mPipelineAsset(pipelineAsset) {
	if (const auto frames = mDescriptor.Execution.FramesInFlight; frames && *frames != static_cast<int>(DriverInterface::settings.framesInFlight)) {
		throw std::invalid_argument("Pipeline Execution.FramesInFlight must match the startup pipeline; select this pipeline at launch");
	}
	const auto backend = DriverInterface::settings.backend;
	const auto visibility
		= ConfigureVgVisibility(mDescriptor, backend == RenderSettings::Backend::VULKAN || backend == RenderSettings::Backend::DIRECTX12);
	mGraphPlan = compileGraph(mDescriptor);
	ValidateVgVisibilitySchedule(mDescriptor, mGraphPlan);
	if (!mPipelineAsset.empty()) {
		mPipelineWriteTime = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().lastWriteTime(mPipelineAsset);
	}
	if (!descriptor.SettingsAsset.empty()) {
		mSettingsWriteTime = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().lastWriteTime(descriptor.SettingsAsset);
	}
	for (const auto& texture : mDescriptor.Textures) {
		mTextures.try_emplace(texture.Name);
		if (texture.History) {
			mTextures.try_emplace(texture.Name + "Previous");
		}
	}
	if (!visibility.Ids.empty()) {
		mVgVisibilityIds = &mTextures.at(visibility.Ids);
		if (visibility.CompatibleGBuffer && renderer.gpuFeatures().FragmentPrimitiveId) {
			try {
				mVgVisibilityShader = RESOURCES::ShaderLoader::Create("/shaders/virtualGeometry/vgVisibility.shader");
			} catch (const std::exception& error) {
				std::cerr << "VG_VISIBILITY_FALLBACK " << error.what() << '\n';
			}
		}
	}
	CreatePipelineBuffers(*this, renderer);
	CreateScreenProbeBuffer(*this, renderer);
	CreateTemporalDenoiseBuffers(*this, renderer);
	mStartStages = loadStages(mDescriptor.StartStages);
	for (const auto& descriptor : mDescriptor.LightPasses) {
		if (mLightPasses.contains(descriptor.Name)) {
			throw std::runtime_error("Duplicate light pass: " + descriptor.Name);
		}
		auto& pass = mLightPasses[descriptor.Name];
		pass.Descriptor = descriptor;
		pass.InitStages = loadStages(descriptor.InitStages);
		pass.CaptureStages = loadStages(descriptor.CaptureStages);
		pass.PrepareStages = loadStages(descriptor.PrepareStages);
		pass.ResolveStages = loadStages(descriptor.ResolveStages);
	}
	mStages = loadStages(mDescriptor.Stages);
	mStageIndex.reserve(mStages.size());
	for (const auto& stage : mStages) {
		if (mDescriptor.Graph.Enabled) {
			ValidateGraphMaterial(*stage, stage->mDescriptor);
		}
		mStageIndex.push_back(stage.get());
	}
}

void GameRenderer::reloadPipelineSettings(const std::string& asset) {
	if (mRenderPipeline) {
		mRenderPipeline->reloadSettings(*mContext.render, asset);
	}
}

void GameRenderer::enableGpuProfiling(bool enabled) {
	mGpuProfiler.setEnabled(enabled);
}
void GameRenderer::resetGpuProfile() {
	mGpuProfiler.reset();
}
PROFILER::GpuProfileSnapshot GameRenderer::gpuProfile() const {
	return mGpuProfiler.snapshot();
}
void GameRenderer::printGpuProfile() const {
	const auto profile = gpuProfile();
	std::cout << "GPU_PROFILE supported=" << profile.Supported << " enabled=" << profile.Enabled << " pending=" << profile.PendingSamples
			  << " dropped=" << profile.DroppedSamples << " query_pairs=" << profile.AllocatedQueryPairs << std::endl;
	for (const auto& stage : profile.Stages) {
		std::cout << "LIGHTING_GPU_STAGE " << stage.Name << " mean_ms=" << stage.MeanMilliseconds << " samples=" << stage.Samples
				  << " median_ms=" << stage.MedianMilliseconds << " p95_ms=" << stage.Percentile95Milliseconds << std::endl;
	}
}

void RenderGraphPipeline::pollSettings(Renderer& renderer) {
	auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	const auto pipelineTime = mPipelineAsset.empty() ? std::filesystem::file_time_type {} : files.lastWriteTime(mPipelineAsset);
	const auto settingsTime
		= mSourceDescriptor.SettingsAsset.empty() ? std::filesystem::file_time_type {} : files.lastWriteTime(mSourceDescriptor.SettingsAsset);
	if (pipelineTime == mPipelineWriteTime && settingsTime == mSettingsWriteTime) {
		return;
	}
	try {
		reloadSettings(renderer);
	} catch (const std::exception& error) {
		std::cerr << "Pipeline settings reload failed: " << error.what() << std::endl;
	}
	// Invalid JSON is reported once per edit; the existing settings remain active.
	mPipelineWriteTime = pipelineTime;
	// A successful reload may have selected a different settings asset.
	mSettingsWriteTime
		= mSourceDescriptor.SettingsAsset.empty() ? std::filesystem::file_time_type {} : files.lastWriteTime(mSourceDescriptor.SettingsAsset);
}

void RenderGraphPipeline::reloadSettings(Renderer& renderer, const std::string& asset) {
	auto source = mSourceDescriptor;
	if (!mPipelineAsset.empty()) {
		auto result = UTILS::FromJson<Descriptor>(mPipelineAsset);
		if (result.isErr()) {
			throw std::runtime_error("Render pipeline: " + result.unwrapErr().text);
		}
		source = result.unwrap();
		auto structure = source, previous = mSourceDescriptor;
		structure.Settings = {};
		previous.Settings = {};
		structure.SettingsAsset.clear();
		previous.SettingsAsset.clear();
		if (UTILS::ToJson(structure).unwrap() != UTILS::ToJson(previous).unwrap()) {
			throw std::runtime_error("Pipeline graph changed; restart to apply resource/stage changes. Only Settings/SettingsAsset can reload live.");
		}
	}
	const auto overrideAsset = asset.empty() ? mSettingsOverrideAsset : asset;
	if (!overrideAsset.empty()) {
		source.SettingsAsset = overrideAsset;
	}
	auto candidate = ResolvePipelineSettings(source);
	const auto backend = DriverInterface::settings.backend;
	ConfigureVgVisibility(candidate, backend == RenderSettings::Backend::VULKAN || backend == RenderSettings::Backend::DIRECTX12);
	auto graphPlan = compileGraph(candidate);
	ValidateVgVisibilitySchedule(candidate, graphPlan);
	if (candidate.Graph.Enabled) {
		for (size_t i = 0; i < candidate.Stages.size(); ++i) {
			ValidateGraphMaterial(*mStageIndex[i], candidate.Stages[i]);
		}
	}
	std::map<const PipelineStage*, PipelineStage::ResolvedBindings> bindings;
	const auto prepareBindings = [&](const StagesArr& stages, const auto& descriptors) {
		auto descriptor = descriptors.begin();
		for (const auto& stage : stages) {
			bindings.emplace(stage.get(), ResolveStageBindings(*this, *stage, *descriptor++));
		}
	};
	prepareBindings(mStartStages, candidate.StartStages);
	prepareBindings(mStages, candidate.Stages);
	for (const auto& descriptor : candidate.LightPasses) {
		const auto& pass = mLightPasses.at(descriptor.Name);
		prepareBindings(pass.InitStages, descriptor.InitStages);
		prepareBindings(pass.CaptureStages, descriptor.CaptureStages);
		prepareBindings(pass.PrepareStages, descriptor.PrepareStages);
		prepareBindings(pass.ResolveStages, descriptor.ResolveStages);
	}

	std::set<std::string> changedTextures, dirty;
	for (size_t i = 0; i < candidate.Textures.size(); ++i) {
		if (UTILS::ToJson(candidate.Textures[i]).unwrap() != UTILS::ToJson(mDescriptor.Textures[i]).unwrap()) {
			changedTextures.insert(candidate.Textures[i].Name);
		}
	}
	dirty = changedTextures;
	bool changed = !dirty.empty();
	auto updateStages = [&](StagesArr& stages, const auto& descriptors) {
		auto descriptor = descriptors.begin();
		for (auto& stage : stages) {
			const auto& next = *descriptor++;
			bool update = UTILS::ToJson(next).unwrap() != UTILS::ToJson(stage->mDescriptor).unwrap();
			std::set<std::string> outputs;
			if (!next.FrameBuffer.empty()) {
				const auto frame = std::find_if(
					candidate.FrameBuffers.begin(), candidate.FrameBuffers.end(), [&](const auto& f) { return f.Name == next.FrameBuffer; });
				outputs.insert(frame->Textures.begin(), frame->Textures.end());
			}
			if (!next.DispatchTexture.empty()) {
				outputs.insert(next.DispatchTexture);
			}
			outputs.insert(next.Writes.begin(), next.Writes.end());
			for (const auto& name : outputs) {
				if (dirty.contains(name)) {
					update = true;
				}
			}
			for (const auto& [name, value] : next.Uniforms) {
				if (const auto* texture = std::get_if<std::string>(&value); texture && dirty.contains(*texture)) {
					update = true;
				}
			}
			stage->mDescriptor = next;
			stage->mResolved = std::move(bindings.at(stage.get()));
			if (update) {
				stage->mExecuted = false;
				dirty.insert(outputs.begin(), outputs.end());
				changed = true;
			}
		}
	};
	updateStages(mStartStages, candidate.StartStages);
	updateStages(mStages, candidate.Stages);
	for (const auto& descriptor : candidate.LightPasses) {
		auto& pass = mLightPasses.at(descriptor.Name);
		pass.Descriptor = descriptor;
		updateStages(pass.InitStages, descriptor.InitStages);
		updateStages(pass.CaptureStages, descriptor.CaptureStages);
		updateStages(pass.PrepareStages, descriptor.PrepareStages);
		updateStages(pass.ResolveStages, descriptor.ResolveStages);
	}
	mDescriptor = std::move(candidate);
	mGraphPlan = std::move(graphPlan);
	mSourceDescriptor = std::move(source);
	mSettingsOverrideAsset = overrideAsset;
	if (changed) {
		mHistory.invalidate(HistoryReason::PipelineReload);
		mIsInitialized = false;
		if ((!changedTextures.empty() || mDescriptor.Graph.Aliasing) && mSize.x > 0 && mSize.y > 0) {
			for (const auto& name : changedTextures) {
				mAllocatedTextures.erase(name);
				mTextureRevisions.erase(name);
			}
			const auto size = mSize;
			mSize = { 0, 0 };
			resize(renderer, size.x, size.y);
		}
		std::cout << "Pipeline settings updated: " << mPipelineAsset << "; SettingsAsset=" << mSourceDescriptor.SettingsAsset << std::endl;
	}
	auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	mSettingsWriteTime
		= mSourceDescriptor.SettingsAsset.empty() ? std::filesystem::file_time_type {} : files.lastWriteTime(mSourceDescriptor.SettingsAsset);
	if (!mPipelineAsset.empty()) {
		mPipelineWriteTime = files.lastWriteTime(mPipelineAsset);
	}
}

void RenderGraphPipeline::invalidateResources(const std::vector<std::string>& resources, bool resetHistory, HistoryReason reason) {
	std::set<std::string> dirty(resources.begin(), resources.end());
	for (auto* stages : stageGroups()) {
		for (auto& stage : *stages) {
			const auto& descriptor = stage->mDescriptor;
			std::set<std::string> outputs;
			if (!descriptor.FrameBuffer.empty()) {
				const auto frame = std::find_if(mDescriptor.FrameBuffers.begin(), mDescriptor.FrameBuffers.end(),
					[&](const auto& f) { return f.Name == descriptor.FrameBuffer; });
				outputs.insert(frame->Textures.begin(), frame->Textures.end());
			}
			if (!descriptor.DispatchTexture.empty()) {
				outputs.insert(descriptor.DispatchTexture);
			}
			bool update = false;
			for (const auto& name : outputs) {
				if (dirty.contains(name)) {
					update = true;
				}
			}
			for (const auto& [name, value] : descriptor.Uniforms) {
				if (const auto* texture = std::get_if<std::string>(&value); texture && dirty.contains(*texture)) {
					update = true;
				}
			}
			if (update) {
				stage->mExecuted = false;
				dirty.insert(outputs.begin(), outputs.end());
			}
		}
	}
	if (resetHistory) {
		mHistory.invalidate(reason, HistoryResourceEffects);
	}
	mIsInitialized = false;
}

void RenderGraphPipeline::pollResourceUpdates() {
	std::vector<std::string> changed;
	for (const auto& texture : mDescriptor.Textures) {
		if (texture.Persistent && mAllocatedTextures.contains(texture.Name)) {
			const auto revision = mAllocatedTextures.at(texture.Name)->getRevision();
			auto [found, inserted] = mTextureRevisions.emplace(texture.Name, revision);
			if (!inserted && found->second != revision) {
				changed.push_back(texture.Name);
				found->second = revision;
			}
		}
	}
	if (!changed.empty()) {
		invalidateResources(changed, true, HistoryReason::ResourceUpdate);
	}
}

void RenderGraphPipeline::prepareUpscaler(Renderer& renderer, int width, int height) {
	if (width <= 0 || height <= 0) {
		return;
	}
	const auto settings = DriverInterface::settings.upscaler;
	if (!std::isfinite(settings.sharpness) || settings.sharpness < 0 || settings.sharpness > 1) {
		throw std::runtime_error("Upscaler sharpness must be in [0,1]");
	}
	auto* upscaleStage = static_cast<PipelineStage*>(nullptr);
	for (auto& stage : mStages) {
		if (stage->mDescriptor.Enabled && stage->mDrawContent == DrawContent::UPSCALE) {
			if (upscaleStage) {
				throw std::runtime_error("A pipeline can have only one active UPSCALE stage");
			}
			upscaleStage = stage.get();
		}
	}
	mRenderSize = { width, height };
	const bool changed
		= !mUpscalerSettingsLoaded || settings.technology != mUpscalerSettings.technology || settings.quality != mUpscalerSettings.quality;
	mUpscalerSettings = settings;
	if (changed) {
		mUpscalerSettingsLoaded = true;
		mUpscalerFailed = false;
		mUpscaler.reset();
		mHistory.invalidate(HistoryReason::PipelineReload);
	}
	if (mUpscalerFailed) {
		mUpscaler.reset();
	}
	if (settings.technology == UpscalerType::OFF || !upscaleStage) {
		if (settings.technology != UpscalerType::OFF && !upscaleStage && changed) {
			std::cerr << "UPSCALER_FALLBACK pipeline has no enabled UPSCALE stage\n";
		}
		mUpscaler.reset();
		mUpscalerStatus = { false, false, UpscalerType::OFF, "Native AA", {} };
		return;
	}
	if (mUpscalerFailed) {
		return;
	}
	try {
		if (!mUpscaler) {
			if (settings.technology == UpscalerType::AUTO) {
				try {
					mUpscaler = renderer.getDriver()->createUpscaler(UpscalerType::DLSS);
				} catch (const UnsupportedUpscaler&) {
					mUpscaler = renderer.getDriver()->createUpscaler(UpscalerType::FSR);
				}
			}
			else {
				mUpscaler = renderer.getDriver()->createUpscaler(settings.technology);
			}
		}
		const MATH::Vector2i display { width, height };
		auto render = mUpscaler->renderSize(display, settings.quality);
		if (render.x <= 0 || render.y <= 0 || render.x > width || render.y > height) {
			throw UnsupportedUpscaler("SDK returned invalid render size");
		}
		mUpscaler->configure(render, display, settings.quality);
		mRenderSize = render;
		mUpscalerStatus = mUpscaler->capabilities();
	} catch (const UnsupportedUpscaler& error) {
		mUpscaler.reset();
		mUpscalerFailed = true;
		mUpscalerStatus = { false, false, settings.technology, {}, error.what() };
		if (upscaleStage->mDescriptor.Unsupported == "ERROR") {
			throw;
		}
		std::cerr << "UPSCALER_FALLBACK " << error.what() << '\n';
	}
}

namespace {
	struct TransientTargets {
		std::map<std::string, std::shared_ptr<TextureInterface>> Textures;
		std::map<GRAPH::PassId, std::vector<std::shared_ptr<TextureInterface>>> Activations;
		std::set<std::string> Names;
		uint64_t AllocationBytes = 0;
		uint64_t SavedBytes = 0;
	};

	TransientTargets AllocateTransientTargets(Renderer& renderer, const GRAPH::Plan& plan, const std::map<std::string, TextureResource>& resources) {
		auto lifetimes = plan.TransientLifetimes;
		std::stable_sort(lifetimes.begin(), lifetimes.end(), [](const auto& a, const auto& b) { return a.First < b.First; });
		using Compatibility = std::tuple<int, int, PixelFormat>;
		using Group = std::vector<const GRAPH::Plan::TransientLifetime*>;
		std::map<Compatibility, std::vector<Group>> groups;
		for (const auto& lifetime : lifetimes) {
			const auto& descriptor = resources.at(lifetime.Name);
			auto& compatible = groups[{ descriptor.width, descriptor.height, descriptor.pixelType }];
			const auto free
				= std::find_if(compatible.begin(), compatible.end(), [&](const auto& group) { return group.back()->Last < lifetime.First; });
			if (free == compatible.end()) {
				compatible.push_back({ &lifetime });
			}
			else {
				free->push_back(&lifetime);
			}
		}
		TransientTargets result;
		for (const auto& [key, compatible] : groups) {
			for (const auto& group : compatible) {
				if (group.size() < 2) {
					continue;
				}
				std::vector<TextureResource> descriptors;
				descriptors.reserve(group.size());
				for (const auto* lifetime : group) {
					descriptors.push_back(resources.at(lifetime->Name));
				}
				auto targets = renderer.getDriver()->createAliasedRenderTargets(descriptors);
				if (targets.Textures.empty()) {
					continue;
				}
				if (targets.Textures.size() != group.size()) {
					throw std::logic_error("Incomplete native render graph alias allocation");
				}
				result.AllocationBytes += targets.AllocationBytes;
				result.SavedBytes += targets.AllocationBytes * (group.size() - 1);
				for (size_t index = 0; index < group.size(); ++index) {
					const auto& lifetime = *group[index];
					result.Activations[plan.Schedule[lifetime.First].Id].push_back(targets.Textures[index]);
					result.Textures.emplace(lifetime.Name, std::move(targets.Textures[index]));
					result.Names.insert(lifetime.Name);
				}
			}
		}
		return result;
	}
}

void RenderGraphPipeline::resize(Renderer& renderer, int width, int height) {
	if (width <= 0 || height <= 0 || (mSize.x == width && mSize.y == height && mAllocatedRenderSize == mRenderSize)) {
		return;
	}
	if (mRenderSize.x <= 0 || mRenderSize.y <= 0) {
		mRenderSize = { width, height };
	}
	std::map<std::string, TextureResource> resources;
	for (const auto& t : mDescriptor.Textures) {
		if (t.Name.empty() || resources.contains(t.Name) || !(t.Scale > 0)) {
			throw std::runtime_error("Invalid/duplicate pipeline texture: " + t.Name);
		}
		auto resource = t.Resource;
		if (t.Size == TextureDesc::SizeBase::FIXED && (!resource.width || !resource.height)) {
			throw std::runtime_error("FIXED texture needs explicit dimensions: " + t.Name);
		}
		const auto base = t.Size == TextureDesc::SizeBase::RENDER ? mRenderSize : MATH::Vector2i { width, height };
		if (!resource.width) {
			resource.width = std::max(1, static_cast<int>(base.x * t.Scale));
		}
		if (!resource.height) {
			resource.height = std::max(1, static_cast<int>(base.y * t.Scale));
		}
		if (t.FullMipChain) {
			if (resource.useMipmap || resource.texType != TextureType::TEXTURE_2D || !t.Path.empty() || t.History) {
				throw std::runtime_error("FullMipChain needs a 2D render texture with UseMipmap false: " + t.Name);
			}
			resource.mipMapCount = static_cast<int>(std::floor(std::log2(std::max(resource.width, resource.height)))) + 1;
		}
		resources.emplace(t.Name, std::move(resource));
	}
	auto transient = mDescriptor.Graph.Aliasing ? AllocateTransientTargets(renderer, mGraphPlan, resources) : TransientTargets {};
	auto textures = std::move(transient.Textures);
	for (const auto& t : mDescriptor.Textures) {
		if (transient.Names.contains(t.Name)) {
			continue;
		}
		if (textures.contains(t.Name)) {
			throw std::runtime_error("Duplicate pipeline texture: " + t.Name);
		}
		if (t.Persistent && mAllocatedTextures.contains(t.Name)
			&& (t.Size == TextureDesc::SizeBase::FIXED || (t.Resource.width && t.Resource.height))) {
			textures[t.Name] = mAllocatedTextures.at(t.Name);
			if (t.History) {
				textures[t.Name + "Previous"] = mAllocatedTextures.at(t.Name + "Previous");
			}
			continue;
		}
		auto create = [&]() {
			return t.Path.empty() ? renderer.createTexture(resources.at(t.Name))
								  : RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().loadResource(t.Path);
		};
		textures[t.Name] = create();
		if (!textures[t.Name]) {
			throw std::runtime_error("Cannot allocate pipeline texture: " + t.Name);
		}
		if (t.History) {
			if (!t.Path.empty()) {
				throw std::runtime_error("History textures must be render targets");
			}
			textures[t.Name + "Previous"] = create();
		}
	}
	for (auto& [name, texture] : textures) {
		mTextures.at(name) = std::move(texture);
	}
	mAllocatedTextures = mTextures;
	mAliasActivations = std::move(transient.Activations);
	mAliasedTextures = std::move(transient.Names);
	mAliasAllocationBytes = transient.AllocationBytes;
	mAliasSavedBytes = transient.SavedBytes;
	if (mDescriptor.Graph.Aliasing) {
		std::cout << "Render graph aliasing: targets=" << mAliasedTextures.size() << " shared_bytes=" << mAliasAllocationBytes
				  << " saved_bytes=" << mAliasSavedBytes << std::endl;
	}
	mSize = { width, height };
	mAllocatedRenderSize = mRenderSize;
	mHistory.invalidate(HistoryReason::Resize);
	mIsInitialized = false;
	rebuildFrameBuffers(renderer);
	const bool history = std::any_of(mDescriptor.Textures.begin(), mDescriptor.Textures.end(), [](const auto& t) { return t.History; });
	if (history) {
		auto current = std::move(mFrameBuffers);
		for (const auto& t : mDescriptor.Textures) {
			if (t.History) {
				std::swap(mAllocatedTextures.at(t.Name), mAllocatedTextures.at(t.Name + "Previous"));
			}
		}
		rebuildFrameBuffers(renderer);
		mAlternateFrameBuffers = std::move(mFrameBuffers);
		mFrameBuffers = std::move(current);
		for (const auto& t : mDescriptor.Textures) {
			if (t.History) {
				std::swap(mAllocatedTextures.at(t.Name), mAllocatedTextures.at(t.Name + "Previous"));
			}
		}
		for (auto* stages : stageGroups()) {
			for (auto& stage : *stages) {
				stage->mFrameBuffer = stage->mDescriptor.FrameBuffer.empty() ? nullptr : mFrameBuffers.at(stage->mDescriptor.FrameBuffer);
			}
		}
	}
}

void RenderGraphPipeline::rebuildFrameBuffers(Renderer& renderer) {
	mFrameBuffers.clear();
	for (const auto& f : mDescriptor.FrameBuffers) {
		if (mFrameBuffers.contains(f.Name)) {
			throw std::runtime_error("Duplicate framebuffer: " + f.Name);
		}
		std::vector<std::shared_ptr<TextureInterface>> colors;
		for (const auto& name : f.Textures) {
			colors.push_back(mAllocatedTextures.at(name));
		}
		auto depth = f.Depth.empty() ? nullptr : mAllocatedTextures.at(f.Depth);
		if (colors.empty() && !depth) {
			throw std::runtime_error("Empty framebuffer: " + f.Name);
		}
		const auto w = std::max<size_t>(1, (depth ? depth->getWidth() : colors.front()->getWidth()) >> f.Mip);
		const auto h = std::max<size_t>(1, (depth ? depth->getHeight() : colors.front()->getHeight()) >> f.Mip);
		if (depth && f.Mip) {
			throw std::runtime_error("Depth attachment mip views are not supported");
		}
		for (const auto& texture : colors) {
			if (f.Mip >= texture->getMipCount() || (texture->getType() == TextureType::TEXTURE_CUBE ? f.Layer >= 6 : f.Layer != 0)) {
				throw std::runtime_error("Invalid framebuffer subresource: " + f.Name);
			}
			if (std::max<size_t>(1, texture->getWidth() >> f.Mip) != w || std::max<size_t>(1, texture->getHeight() >> f.Mip) != h) {
				throw std::runtime_error("Framebuffer size mismatch: " + f.Name);
			}
		}
		mFrameBuffers[f.Name] = renderer.createFrameBuffer(colors, depth, f.Mip, f.Layer);
	}
	for (auto* stages : stageGroups()) {
		for (auto& stage : *stages) {
			stage->mFrameBuffer = stage->mDescriptor.FrameBuffer.empty() ? nullptr : mFrameBuffers.at(stage->mDescriptor.FrameBuffer);
		}
	}
}

std::vector<RenderGraphPipeline::StagesArr*> RenderGraphPipeline::stageGroups() {
	std::vector<StagesArr*> groups = { &mStartStages, &mStages };
	for (auto& [name, pass] : mLightPasses) {
		groups.push_back(&pass.InitStages);
		groups.push_back(&pass.CaptureStages);
		groups.push_back(&pass.PrepareStages);
		groups.push_back(&pass.ResolveStages);
	}
	return groups;
}

GameRenderer::RayFrameData GameRenderer::rayFrameData() const {
	const auto& history = mRenderPipeline->mHistory;
	const auto flag = [](bool value) { return value ? 1.f : 0.f; };
	RayFrameData data;
	data.State = { static_cast<float>(mRaySample), flag(history.allValid(HistoryRayTracedEffects)), static_cast<float>(mFrameCount),
		mRaySceneChanged ? 0.f : 1.f };
	data.Motion = { flag(history.sceneMoved()), flag(history.isValid(HistoryEffect::GlobalIllumination)),
		flag(history.isValid(HistoryEffect::Reflections)), flag(history.isValid(HistoryEffect::Shadows)) };
	return data;
}

HistoryCounters GameRenderer::historyCounters() const {
	auto total = mRetiredHistoryCounters;
	if (mRenderPipeline) {
		total.add(mRenderPipeline->mHistory.counters());
	}
	return total;
}

void GameRenderer::logHistoryCounters(bool force) {
	const auto total = historyCounters();
	if (!force && total == mLoggedHistoryCounters) {
		return;
	}
	mLoggedHistoryCounters = total;
	for (size_t effect = 0; effect < HistoryEffectCount; ++effect) {
		const auto* prefix = historyEffectLogPrefix(static_cast<HistoryEffect>(effect));
		for (size_t reason = 0; reason < HistoryReasonCount; ++reason) {
			const auto* name = historyReasonName(static_cast<HistoryReason>(reason));
			// GI lines are always printed (including zero counts) so tests can assert them.
			if (total.Resets[effect][reason] != 0 || effect == static_cast<size_t>(HistoryEffect::GlobalIllumination)) {
				std::cout << prefix << "_HISTORY_RESET reason=" << name << " count=" << total.Resets[effect][reason] << " frame=" << mFrameCount
						  << '\n';
			}
			if (total.Kept[effect][reason] != 0 && effect != static_cast<size_t>(HistoryEffect::Presentation)
				&& effect != static_cast<size_t>(HistoryEffect::Upscaler)) {
				std::cout << prefix << "_HISTORY_KEPT reason=" << name << " count=" << total.Kept[effect][reason] << " frame=" << mFrameCount << '\n';
			}
		}
	}
	std::cout.flush();
}

GameRenderer::~GameRenderer() {
	logHistoryCounters(true);
}

void RenderGraphPipeline::advanceHistory(Renderer& renderer) {
	bool changed = false;
	for (const auto& texture : mDescriptor.Textures) {
		if (texture.History) {
			std::swap(mAllocatedTextures.at(texture.Name), mAllocatedTextures.at(texture.Name + "Previous"));
			changed = true;
		}
	}
	mHistory.markValid(HistoryRayTracedEffects);
	if (changed) {
		std::swap(mFrameBuffers, mAlternateFrameBuffers);
		for (auto* stages : stageGroups()) {
			for (auto& stage : *stages) {
				stage->mFrameBuffer = stage->mDescriptor.FrameBuffer.empty() ? nullptr : mFrameBuffers.at(stage->mDescriptor.FrameBuffer);
			}
		}
	}
}

RenderGraphPipeline::StagesArr RenderGraphPipeline::loadStages(const std::vector<PipelineStage::Descriptor>& descriptors) {
	StagesArr result;
	std::set<std::string> names;
	for (const auto& desc : descriptors) {
		if (!names.insert(desc.Name).second) {
			throw std::runtime_error("Duplicate pipeline stage: " + desc.Name);
		}
		auto stage = std::make_unique<PipelineStage>();
		if (desc.Draw == DrawContent::FRAME_GENERATION) {
			if (desc.Once || desc.UpdateInterval != 1 || !desc.PresentationSink) {
				throw std::runtime_error("FRAME_GENERATION must be a per-frame presentation sink");
			}
			if (desc.Unsupported != "BYPASS" && desc.Unsupported != "ERROR") {
				throw std::runtime_error("Invalid FRAME_GENERATION unsupported policy");
			}
			for (const char* key : { "Color", "Depth", "MotionVectors", "UI" }) {
				if (!desc.Bindings.contains(key)) {
					throw std::runtime_error("Missing FRAME_GENERATION binding: " + std::string(key));
				}
			}
		}
		if (desc.Draw == DrawContent::VIRTUAL_GEOMETRY_POST || desc.Draw == DrawContent::GPU_SCENE_OCCLUSION_POST) {
			ValidateGeometryOcclusionPostStage(desc);
		}
		stage->mDescriptor = desc;
		stage->mName = desc.Name;
		stage->mDrawContent = desc.Draw;
		if (desc.Draw == DrawContent::UPSCALE) {
			if (desc.Once || desc.UpdateInterval != 1 || !desc.Program.empty()) {
				throw std::runtime_error("UPSCALE must execute once each frame: " + desc.Name);
			}
			if (desc.Unsupported != "ERROR" && desc.Unsupported != "BYPASS") {
				throw std::runtime_error("Unknown UPSCALE Unsupported policy");
			}
			for (const char* key : { "Color", "Depth", "MotionVectors", "Output" }) {
				if (!desc.Bindings.contains(key)) {
					throw std::runtime_error("Missing UPSCALE binding: " + std::string(key));
				}
			}
			if (desc.Material.empty() || desc.FrameBuffer.empty()) {
				throw std::runtime_error("UPSCALE needs fallback Material/FrameBuffer");
			}
			const auto output = desc.Bindings.at("Output");
			const auto resource
				= std::find_if(mDescriptor.Textures.begin(), mDescriptor.Textures.end(), [&](const auto& t) { return t.Name == output; });
			if (resource == mDescriptor.Textures.end() || !resource->Resource.storage || resource->Size != TextureDesc::SizeBase::DISPLAY) {
				throw std::runtime_error("UPSCALE output must be a DISPLAY storage texture");
			}
			if (resource->Scale != 1 || resource->Resource.width || resource->Resource.height) {
				throw std::runtime_error("UPSCALE output must follow the complete display extent");
			}
			const auto frame = std::find_if(
				mDescriptor.FrameBuffers.begin(), mDescriptor.FrameBuffers.end(), [&](const auto& f) { return f.Name == desc.FrameBuffer; });
			if (frame == mDescriptor.FrameBuffers.end() || frame->Textures != std::vector<std::string> { output } || !frame->Depth.empty()) {
				throw std::runtime_error("UPSCALE fallback framebuffer must contain its output only");
			}
		}
		if (desc.Draw == DrawContent::RAY_TRACE || (desc.Draw == DrawContent::COMPUTE && !desc.Program.empty() && !desc.Compute.usesExtensions())) {
			if (desc.Program.empty() || !desc.Material.empty()) {
				throw std::runtime_error("RAY_TRACE requires Program and does not accept Material: " + desc.Name);
			}
			if (desc.LaunchX <= 0 || desc.LaunchY <= 0 || desc.LaunchZ <= 0 || desc.RaygenRecord < 0 || desc.HitGroupOffset < 0) {
				throw std::runtime_error("Invalid RT launch/record: " + desc.Name);
			}
			if (desc.Unsupported != "ERROR" && desc.Unsupported != "BYPASS") {
				throw std::runtime_error("Unknown RT Unsupported policy: " + desc.Name);
			}
			auto& renderer = RESOURCES::ServiceManager::Get<Renderer>();
			if (desc.Enabled) {
				try {
					const auto caps = renderer.getDriver()->rayTracingCapabilities();
					if (desc.Draw == DrawContent::COMPUTE ? !caps.RayQuery : !caps.Pipeline) {
						throw UnsupportedRayTracing("required ray tracing capability unavailable on selected backend/GPU");
					}
					stage->mRayTracingProgram = LoadRayTracingProgram(renderer, desc.Program);
					if (stage->mRayTracingProgram->isRayQuery() != (desc.Draw == DrawContent::COMPUTE)) {
						throw std::runtime_error("RT program kind disagrees with Draw: " + desc.Name);
					}
				} catch (const UnsupportedRayTracing& error) {
					if (desc.Unsupported == "ERROR") {
						throw std::runtime_error("RAY_TRACE stage " + desc.Name + ": " + error.what());
					}
					for (const auto& output : desc.Writes) {
						if (!desc.Bypass.contains(output)) {
							throw std::runtime_error("Unsupported RT stage needs explicit Bypass for output " + output);
						}
					}
					std::cout << "RAY_TRACING_FALLBACK " << desc.Name << ": " << error.what() << "\n";
				}
			}
		}
		if (BypassUnsupportedComputeStageBeforeLoad(*this, *stage)) {
			result.push_back(std::move(stage));
			continue;
		}
		if (!desc.Material.empty()) {
			try {
				stage->mMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(desc.Material);
			} catch (const std::exception& error) {
				std::cerr << "Pipeline stage " << desc.Name << " (" << desc.Material << "): " << error.what() << std::endl;
				throw;
			}
			if (!stage->mMaterial || !stage->mMaterial->hasShader()) {
				throw std::runtime_error("Invalid stage material: " + desc.Material);
			}
			for (const auto& uniform : stage->mMaterial->getShader()->getReflection().mUniforms) {
				if (stage->mMaterial->isEngineUniform(uniform.mName)) {
					continue;
				}
				if (uniform.mMembers.empty()) {
					if (const auto* value = stage->mMaterial->findUniform(uniform.mName)) {
						stage->mDefaults[uniform.mName] = *value;
					}
				}
				else {
					for (const auto& member : uniform.mMembers) {
						const auto key = uniform.mName + member.mName;
						if (const auto* value = stage->mMaterial->findUniform(key)) {
							stage->mDefaults[key] = *value;
						}
					}
				}
			}
		}
		if (desc.Compute.usesExtensions()) {
			LoadComputeStage(*this, *stage);
		}
		stage->mResolved = ResolveStageBindings(*this, *stage, desc);
		if (desc.VirtualGeometryShadows.Enabled) {
			VG::ValidateVirtualGeometryShadowSettings(desc.VirtualGeometryShadows);
			if (desc.Draw != DrawContent::SHADOW || desc.ShadowType != "directional" || !desc.OverrideMaterial || !stage->mMaterial
				|| stage->mMaterial->getShader()->mPath != "/shaders/render/portable/shadow.shader" || stage->mMaterial->isBlendable()
				|| !desc.Uniforms.empty() || !desc.BufferOverrides.empty() || !desc.BufferLinks.empty()) {
				throw std::invalid_argument("VirtualGeometryShadows requires the stock directional shadow stage: " + desc.Name);
			}
			const auto* passAlpha = stage->mMaterial->findUniform("data_UBOu_Albedo");
			const auto* color = passAlpha ? std::get_if<MATH::Vector4f>(passAlpha) : nullptr;
			const auto* passTexture = stage->mMaterial->findUniform("u_AlbedoMap");
			const auto* texture = passTexture ? std::get_if<std::shared_ptr<TextureInterface>>(passTexture) : nullptr;
			if (desc.Enabled && color && color->w >= 0.5f && (!passTexture || (texture && !*texture)) && VG::IsVirtualGeometryGpuPathSupported()) {
				try {
					stage->mVgShadowShader = RESOURCES::ShaderLoader::Create("/shaders/virtualGeometry/vgDirectionalShadow.shader");
				} catch (const std::exception& error) {
					std::cerr << "VG_SHADOW_FALLBACK " << desc.Name << ": " << error.what() << '\n';
				}
			}
		}
		result.push_back(std::move(stage));
	}
	return result;
}

void GameRenderer::setDrawStage(PipelineStage& stage) {
	mOverrideMaterial = stage.mDescriptor.OverrideMaterial ? stage.mMaterial : nullptr;
	mOverrideDefaults = stage.mDefaults;
	mActiveStage = &stage;
	for (const auto& [name, value] : stage.mResolved.Constants) {
		mOverrideDefaults[name] = value;
	}
}

void GameRenderer::bindStage(PipelineStage& stage) {
	auto& renderer = *mContext.render;
	auto material = stage.mMaterial;
	const auto shader = material->getShader();
	if (stage.mResolved.Shader != shader || stage.mResolved.Generation != shader->generation()) {
		if (mRenderPipeline->mDescriptor.Graph.Enabled) {
			ValidateGraphMaterial(stage, stage.mDescriptor);
		}
		stage.mResolved = ResolveStageBindings(*mRenderPipeline, stage, stage.mDescriptor);
	}
	const auto& reflection = shader->getReflection();
	for (const auto& [name, value] : stage.mDefaults) {
		material->set(name, value);
	}
	for (const auto& [name, value] : stage.mResolved.Constants) {
		material->set(name, value);
	}
	renderer.setShader(shader);
	renderer.setDepth({ material->isDepthTest() ? material->getDepthFunc() : DepthFunction::ALWAYS, material->isDepthWriting() });
	renderer.setCull(material->isBackfaceCulling() ? CullFace::BACK : CullFace::NONE);
	if (stage.mDescriptor.Additive) {
		renderer.setBlending({ BlendMode::ONE, BlendMode::ONE });
	}
	else if (material->isBlendable()) {
		renderer.setBlending({ BlendMode::SRC_ALPHA, BlendMode::ONE_MINUS_SRC_ALPHA });
	}
	else {
		renderer.resetBlending();
	}
	material->bind(mEmptyTexture, true);
	for (const auto& texture : stage.mResolved.Textures) {
		renderer.setTexture(texture.Binding, *texture.Slot);
	}
	for (const auto& buffer : stage.mResolved.UniformBuffers) {
		renderer.setUniformBuffer(buffer.Binding, *buffer.Slot);
	}
	for (const auto& buffer : stage.mResolved.StorageBuffers) {
		renderer.setStorageBuffer(buffer.Binding, *buffer.Slot);
	}
	for (const auto binding : stage.mResolved.Lights) {
		renderer.setStorageBuffer(binding, mLightSSBO);
	}
	for (auto& buffer : stage.mResolved.Overrides) {
		if (!buffer.Buffer) {
			buffer.Buffer = renderer.createUniformBuffer(buffer.Bytes.data(), buffer.Bytes.size());
		}
		renderer.setUniformBuffer(buffer.Binding, buffer.Buffer);
	}
	for (const auto& [name, buffer] :
		std::map<std::string, std::shared_ptr<UniformBufferInterface>> { { "engine_RayFrame", mRayFrameUbo }, { "engine_UBO", mEngineUbo },
		{ "engine_Frame", mFrameUbo }, { "engine_ShadowData", mShadowUbo }, { "engine_CurrentShadow", mCurrentShadowUbo } }) {
		if (reflection.mNameToUniforms.contains(name)) {
			renderer.setUniformBuffer(name, buffer);
		}
	}
	if (reflection.mNameToUniforms.contains("engine_Lights")) {
		renderer.setStorageBuffer("engine_Lights", mLightSSBO);
	}
	bindEnvironment(renderer, reflection);
}

void GameRenderer::renderGui(SCENE_SYSTEM::Scene& scene, bool worldVectors) {
	auto& renderer = *mContext.render;
	auto& loader = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>();
	if (!mGuiSpriteMaterial) {
		mGuiSpriteMaterial = loader.loadResource("/materials/render/portable/gui.mat");
	}
	if (!mGuiTextMaterial) {
		mGuiTextMaterial = loader.loadResource("/materials/render/portable/text.mat");
	}
	auto projection = MATH::Matrix4f::CreateOrthographic(0, uboData.ViewportSize.x, uboData.ViewportSize.y, 0, -1, 1);
	if (DriverInterface::settings.backend != RenderSettings::Backend::OPENGL) {
		auto correction = MATH::Matrix4f::Identity;
		correction(2, 2) = .5f;
		correction(2, 3) = .5f;
		projection = correction * projection;
	}
	if (!mVectorRenderer) {
		mVectorRenderer = std::make_shared<VECTOR::VectorRenderer>();
	}
	mVectorRenderer->beginFrame(worldVectors);
	Scissor guiClip;
	bool guiClipped = false;
	auto submit
		= [&](const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, const std::shared_ptr<TextureInterface>& texture,
			  const std::shared_ptr<MaterialInterface>& material, const MATH::Matrix4f& transform, bool is3D,
			  Blending blend = Blending(BlendMode::SRC_ALPHA, BlendMode::ONE_MINUS_SRC_ALPHA, BlendMode::ONE, BlendMode::ONE_MINUS_SRC_ALPHA)) {
			  if (!texture || vertices.empty()) {
				  return;
			  }
			  auto transformed = vertices;
			  for (auto& v : transformed) {
				  auto p = transform * MATH::Vector4f(v.position.x, v.position.y, v.position.z, 1);
				  v.position = { p.x, p.y, p.z };
			  }
			  mVectorRenderer->enqueue(std::move(transformed), indices, texture, material->getShader(),
				  is3D ? uboData.View * uboData.Projection : MATH::Matrix4f::Transpose(projection), blend, { DepthFunction::ALWAYS, false }, guiClip,
				  guiClipped);
		  };
	auto quad = [](float x, float y, float w, float h, AtlasRect uv, MATH::Vector4f color) {
		std::vector<Vertex> vertices = { Vertex({ x, y, 0 }, { uv.mX, uv.mY }), Vertex({ x + w, y, 0 }, { uv.mX + uv.mW, uv.mY }),
			Vertex({ x + w, y + h, 0 }, { uv.mX + uv.mW, uv.mY + uv.mH }), Vertex({ x, y + h, 0 }, { uv.mX, uv.mY + uv.mH }) };
		for (auto& vertex : vertices) {
			vertex.m_Weights = color;
		}
		return vertices;
	};
	auto sceneObjects = scene.getObjects();
	std::vector<std::shared_ptr<ECS::Object>> objects(sceneObjects.begin(), sceneObjects.end());
	auto canvasFor = [](auto object) {
		while (object) {
			if (auto canvas = object->template getComponent<ECS::VectorCanvasComponent>()) {
				return canvas;
			}
			object = object->getParent();
		}
		return decltype(object->template getComponent<ECS::VectorCanvasComponent>()) {};
	};
	auto orderFor = [&](auto object) {
		auto canvas = canvasFor(object);
		int order = canvas ? canvas->Order : 0, layer = canvas ? canvas->SortingLayer : 0;
		if (auto c = object->template getComponent<ECS::VectorShapeComponent>()) {
			order += c->Order;
		}
		else if (auto c = object->template getComponent<ECS::VectorPathComponent>()) {
			order += c->Order;
		}
		else if (auto c = object->template getComponent<ECS::SdfTextComponent>()) {
			order += c->Order;
		}
		return std::pair { layer, order };
	};
	std::stable_sort(objects.begin(), objects.end(), [&](auto a, auto b) { return orderFor(a) < orderFor(b); });
	for (auto& object : objects) {
		if (!object->getIsActive() || !object->getIsVisible()) {
			continue;
		}
		auto canvas = canvasFor(object);
		bool world = canvas && canvas->WorldSpace;
		if (worldVectors && !world) {
			continue;
		}
		auto canvasCorrection = MATH::Matrix4f::Identity;
		if (canvas && canvas->DpiScale != 1.f) {
			auto root = canvas->obj->getTransform()->getTransform().getWorldMatrix();
			auto scale = MATH::Matrix4f::Identity;
			scale(0, 0) = scale(1, 1) = std::clamp(canvas->DpiScale, .01f, 100.f);
			canvasCorrection = root * scale * MATH::Matrix4f::Inverse(root);
		}
		float left = 0, top = 0, right = uboData.ViewportSize.x, bottom = uboData.ViewportSize.y;
		bool clipped = false;
		for (auto ancestor = object; ancestor; ancestor = ancestor->getParent()) {
			if (auto clip = ancestor->getComponent<ECS::ClipComponent>()) {
				auto matrix = canvasCorrection * ancestor->getTransform()->getTransform().getWorldMatrix();
				float minX = right, minY = bottom, maxX = left, maxY = top;
				for (const auto& corner : { MATH::Vector4f(0, 0, 0, 1), MATH::Vector4f(clip->mWidth, 0, 0, 1), MATH::Vector4f(0, clip->mHeight, 0, 1),
					 MATH::Vector4f(clip->mWidth, clip->mHeight, 0, 1) }) {
					auto local = corner;
					if (worldVectors) {
						local.y = -local.y;
					}
					auto point = matrix * local;
					if (worldVectors) {
						auto camera = MATH::Matrix4f::Transpose(uboData.View * uboData.Projection);
						point = camera * point;
						if (point.w <= 1e-4f) {
							continue;
						}
						point.x = (point.x / point.w * .5f + .5f) * uboData.ViewportSize.x;
						point.y = (.5f - point.y / point.w * .5f) * uboData.ViewportSize.y;
					}
					minX = std::min(minX, point.x);
					minY = std::min(minY, point.y);
					maxX = std::max(maxX, point.x);
					maxY = std::max(maxY, point.y);
				}
				left = std::max(left, minX);
				top = std::max(top, minY);
				right = std::min(right, maxX);
				bottom = std::min(bottom, maxY);
				clipped = true;
			}
		}
		if (clipped) {
			if (right <= left || bottom <= top) {
				continue;
			}
			guiClip = { { left, top }, { right - left, bottom - top } };
			guiClipped = true;
		}
		else {
			guiClipped = false;
		}
		if (!worldVectors && (object->getComponent<ECS::ParticleSystem2DComponent>() || object->getComponent<ECS::SpriteParticleComponent>())) {
			mVectorRenderer->flushPending(renderer);
			if (!mParticleRenderer) {
				mParticleRenderer = std::make_shared<ParticleRenderer>();
			}
			mParticleRenderer->drawScreen(*object, renderer, MATH::Matrix4f::Transpose(projection), guiClip, guiClipped, canvasCorrection);
		}
		const auto& transform = object->getTransform()->getTransform();
		auto animatedSprite = object->getComponent<ECS::SpriteAnimateComponent>();
		auto plainSprite = object->getComponent<ECS::SpriteComponent>();
		ECS::SpriteComponent* sprite = plainSprite ? plainSprite.get() : nullptr;
		if (!sprite && animatedSprite) {
			sprite = animatedSprite.get();
		}
		if (!worldVectors && sprite && sprite->mTexture) {
			const auto size = transform.getLocalSize();
			auto uv = sprite->mTexture->getPieceUV(sprite->mTexturePiece);
			if (animatedSprite) {
				animatedSprite->updateAnim();
				uv = animatedSprite->mUVRect;
			}
			// Image decoders store images bottom-up; GUI coordinates grow downwards.
			uv.mY = 1 - uv.mY;
			uv.mH = -uv.mH;
			submit(quad(0, 0, size.x, size.y, uv, sprite->mColor), { 0, 1, 2, 2, 3, 0 }, sprite->mTexture, mGuiSpriteMaterial,
				transform.getWorldMatrix(), sprite->mIs3D);
		}
		if (auto label = object->getComponent<ECS::LabelComponent>(); !worldVectors && label && label->mFont && label->mFont->texture) {
			std::vector<Vertex> vertices;
			std::vector<unsigned> indices;
			float x = 0;
			for (char c : label->mLabel) {
				const auto found = label->mFont->Characters.find(c);
				if (found == label->mFont->Characters.end()) {
					continue;
				}
				const auto& ch = found->second;
				const float width = static_cast<float>(label->mFont->texture->getWidth()),
							height = static_cast<float>(label->mFont->texture->getHeight());
				auto v = quad(x + ch.Bearing.x, -ch.Bearing.y, ch.Size.x, ch.Size.y,
					{ ch.Start.x / width, ch.Start.y / height, ch.Size.x / width, ch.Size.y / height }, label->color);
				const auto base = static_cast<unsigned>(vertices.size());
				vertices.insert(vertices.end(), v.begin(), v.end());
				for (unsigned index : { 0, 1, 2, 2, 3, 0 }) {
					indices.push_back(base + index);
				}
				x += ch.Advance / 64.0f;
			}
			submit(vertices, indices, label->mFont->texture, mGuiTextMaterial, transform.getWorldMatrix(), label->mIs3D);
		}
		if (auto component = object->getComponent<ECS::SpineComponent>();
			!worldVectors && component && component->spine && component->spine->_drawable) {
			auto* skeleton = component->spine->_drawable;
			skeleton->submit = [&](const std::vector<SPINE::SpineVertex>& source, const SPINE::SpineRenderState& state) {
				std::vector<Vertex> vertices;
				std::vector<unsigned> indices;
				for (const auto& v : source) {
					Vertex vertex({ v.x, -v.y, 0 }, { v.u, v.v });
					const float alpha = skeleton->getUsePremultipliedAlpha() ? v.a : 1.0f;
					vertex.m_Weights = { v.r * alpha, v.g * alpha, v.b * alpha, v.a };
					indices.push_back(static_cast<unsigned>(vertices.size()));
					vertices.push_back(vertex);
				}
				submit(vertices, indices, SPINE::SpineTextureLoader::getTexture(state.texture), mGuiSpriteMaterial, transform.getWorldMatrix(),
					component->mIs3D, { state.blendSrc, state.blendDst });
			};
			skeleton->draw();
			skeleton->submit = {};
		}
		if (world != worldVectors) {
			continue;
		}
		float pixelScale = 1;
		auto matrix = canvasCorrection * transform.getWorldMatrix();
		if (world) {
			auto camera = MATH::Matrix4f::Transpose(uboData.View * uboData.Projection);
			auto project = [&](MATH::Vector4f p) {
				auto v = camera * (matrix * p);
				if (v.w <= 1e-4f) {
					return MATH::Vector2f {};
				}
				return MATH::Vector2f(v.x / v.w * uboData.ViewportSize.x * .5f, v.y / v.w * uboData.ViewportSize.y * .5f);
			};
			auto origin = project({ 0, 0, 0, 1 }), x = project({ 1, 0, 0, 1 }) - origin, y = project({ 0, 1, 0, 1 }) - origin;
			pixelScale = std::max(std::sqrt(x.x * x.x + x.y * x.y), std::sqrt(y.x * y.x + y.y * y.y));
		}
		else {
			pixelScale = std::max(std::sqrt(matrix(0, 0) * matrix(0, 0) + matrix(1, 0) * matrix(1, 0)),
				std::sqrt(matrix(0, 1) * matrix(0, 1) + matrix(1, 1) * matrix(1, 1)));
		}
		for (auto draw : mVectorRenderer->prepare(*object, pixelScale, renderer)) {
			if (!draw.mesh || draw.mesh->indices.empty()) {
				continue;
			}
			auto& material = draw.sdf ? mSdfMaterial : mVectorMaterial;
			if (!material) {
				material = loader.loadResource(draw.sdf ? "/materials/render/portable/sdfText.mat" : "/materials/render/portable/vector.mat");
			}
			std::vector<Vertex> vertices;
			vertices.reserve(draw.mesh->vertices.size());
			for (auto& v : draw.mesh->vertices) {
				auto p = matrix * MATH::Vector4f(v.position.x, world ? -v.position.y : v.position.y, 0, 1);
				Vertex vertex({ p.x, p.y, p.z }, { v.uv.x, v.uv.y });
				vertex.m_Weights = { v.color.r, v.color.g, v.color.b, v.color.a };
				vertex.normal = { v.uvMin.x, v.uvMin.y, 0 };
				vertex.tangent = { v.uvMax.x, v.uvMax.y, 0 };
				vertices.push_back(vertex);
			}
			mVectorRenderer->enqueue(std::move(vertices), draw.mesh->indices, draw.texture, material->getShader(),
				world ? uboData.View * uboData.Projection : MATH::Matrix4f::Transpose(projection), { BlendMode::ONE, BlendMode::ONE_MINUS_SRC_ALPHA },
				{ world && canvas->DepthTest ? DepthFunction::LESS_EQUAL : DepthFunction::ALWAYS, false }, guiClip, guiClipped, draw.sdf,
				draw.effects);
		}
	}
	mVectorRenderer->flush(renderer);
}
}
