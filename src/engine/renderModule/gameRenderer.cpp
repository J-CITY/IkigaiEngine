#include "gameRenderer.h"

#include <coreModule/graphicsWrapper.hpp>
#include "backends/interface/frameBufferInterface.h"
#include "coreModule/core/core.h"
#include "resourceModule/serviceManager.h"

#include "resourceModule/materialManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include "sceneModule/sceneManager.h"

#include "backends/interface/indexBufferInterface.h"
#include "backends/interface/uniformTypes.h"
#include "backends/interface/vertexBufferInterface.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/uniformBufferInterface.h"

#include "renderModule/render.h"
#include "renderModule/light.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "skeletalModule/animationOffset.h"
#include "skeletalModule/animationTransform.h"
#include "skeletalModule/iAnimationPlayable.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/time/time.h"
#include <stdexcept>
#include "windowModule/window/window.h"
#include <iostream>

#ifdef OCULUS
#include "util_matrix.h"
#include "xr_linear.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/object.h"
#endif

namespace IKIGAI::RENDER {

	GameRenderer::GameRenderer(CORE::Core& context) : mContext(context) {
		Init();
	}

	void GameRenderer::Init() {
		auto& render = mContext.render;

		mEmptyMaterial = nullptr;
		//mEmptyMaterial = render->createMaterial();
		//ShaderResource shaderRes;
		//mEmptyMaterial->setShader(render->createShader(shaderRes));
		//mEmptyMaterial->set("u_Diffuse", MATH::Vector4(1.f, 0.f, 1.f, 1.f));

#if defined(USING_GLES) && !defined(IKIGAI_GLES_HAS_SSBO)
		mLightSSBO = render->createStorageBuffer(nullptr, MAX_LIGHTS, sizeof(LightOGL));
#else
		mLightSSBO = render->createStorageBuffer(nullptr, 0, 0);
#endif
		mEmptyTexture = render->createTexture("/textures/empty.png");

		mEngineUbo = render->createUniformBuffer<EngineUBO>({});
		mBoneUbo = render->createUniformBuffer<BonesUBO>({});


		RenderGraphPipeline::Descriptor desc;

		PipelineStage::Descriptor stageDesc;
		stageDesc.Draw = DrawContent::FORWARD;
		stageDesc.Name = "Forward";
		desc.Stages.push_back(stageDesc);

		setPipeline(std::make_unique<RenderGraphPipeline>(desc, *render));
	}

#ifdef __EMSCRIPTEN__
	int gRendererLogFrames = 8;
#define WEB_LOG(msg) do { if (gRendererLogFrames > 0) { std::cout << "[web] " << msg << std::endl; std::cout.flush(); } } while (0)
#else
#define WEB_LOG(msg) do {} while (0)
#endif

	void GameRenderer::renderScene() {
		WEB_LOG("GameRenderer::renderScene");
		auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
		auto& render = mContext.render;

		auto camera = scene.findMainCamera();

		if (auto mainCameraComponent = mContext.sceneManager->getCurrentScene().findMainCamera()) {
			WEB_LOG("GameRenderer camera found");
			auto sz = mContext.window->getSize();
			auto winWidth = sz.x;
			auto winHeight = sz.y;
			if (winWidth > 0 && winHeight > 0) {
				render->getDriver()->resize(winWidth, winHeight);
			}

			const auto& transform = mainCameraComponent->obj->getTransform();
			const auto& cameraPosition = transform->getWorldPosition();
			const auto& cameraRotation = transform->getWorldRotation();
			mainCameraComponent->getCamera().cacheMatrices(winWidth, winHeight, cameraPosition, cameraRotation);

			//const auto glState = mDriver->fetchGLState();
			renderScene(scene, *mainCameraComponent);
			//mDriver->applyStateMask(glState);
		}
		else {
			WEB_LOG("GameRenderer no camera, clear red");
			render->setClearColor(1.0f, 0.0f, 0.0f, 1.0f);
			render->clear(true, true, false);
		}
		mFrameCount++;
		WEB_LOG("GameRenderer::renderScene done");
#ifdef __EMSCRIPTEN__
		if (gRendererLogFrames > 0) {
			--gRendererLogFrames;
		}
#endif
	}

	void GameRenderer::drawDrawable(const Drawable& drawable) {
		WEB_LOG("drawDrawable");
		static int drawLogs = 8;
		static int skipLogs = 4;
		const bool canDraw = drawable.material && drawable.material->hasShader() && drawable.material->getGPUInstances() > 0 && drawable.mesh;
		if (skipLogs > 0 && !canDraw) {
			--skipLogs;
			IKIGAI_COUT("draw skip material=" << (drawable.material ? 1 : 0)
				<< " shader=" << (drawable.material && drawable.material->hasShader() ? 1 : 0)
				<< " instances=" << (drawable.material ? drawable.material->getGPUInstances() : 0)
				<< " mesh=" << (drawable.mesh ? 1 : 0));
		}
		if (drawable.material->hasShader() && drawable.material->getGPUInstances() > 0) {
			auto& render = mContext.render;

			render->setDepth(drawable.material->getDepthFunc());
			//render->setCull(CullFace::NONE);

			render->setShader(drawable.material->getShader());
			drawable.material->bind(mEmptyTexture, true);

			//TODO: use render for it
			render->setPushConstant(IKIGAI::RENDER::ShaderType::VERTEX, 0, sizeof(MATH::Matrix4f), &drawable.world);

			BonesUBO data;
			if (drawable.skeleton && drawable.animationPlayable &&
				drawable.mAnimLocalTransform && drawable.mAnimGlobalTransform && drawable.mAnimOffset) {
#ifdef __EMSCRIPTEN__
				static bool loggedSkin = false;
				if (!loggedSkin) {
					loggedSkin = true;
					std::cout << "[web] skinning pose joints=" << drawable.skeleton->getNumJolts()
						<< std::endl;
					std::cout.flush();
				}
#endif
				data.use = 1;
				{
					auto final_pose = drawable.animationPlayable->getPose();
					auto* local_transforms = drawable.mAnimLocalTransform->generateTransforms(final_pose);
					auto* global_transforms = drawable.mAnimGlobalTransform->generateTransforms(local_transforms);
					auto* final_transforms = drawable.mAnimOffset->offset(global_transforms);
					std::memcpy(data.bones, final_transforms->transforms, sizeof(MATH::Matrix4f) * 128);
					for (int i = 0; i < 128; ++i) {
						data.bones[i] = MATH::Matrix4f::Transpose(data.bones[i]);
					}
				}
			}
#ifdef __EMSCRIPTEN__
			else if (drawable.skeleton || drawable.animationPlayable) {
				static bool loggedNoSkin = false;
				if (!loggedNoSkin) {
					loggedNoSkin = true;
					std::cout << "[web] no skinning skeleton=" << (drawable.skeleton ? "ok" : "null")
						<< " playable=" << (drawable.animationPlayable ? "ok" : "null") << std::endl;
					std::cout.flush();
				}
			}
#endif
			mBoneUbo->setData(data);

			//std::static_pointer_cast<ShaderGl>(drawable.material->getShader())->setMat4("engine_Model.Projection", MATH::Matrix4f::Transpose(uboData.Projection));
			//std::static_pointer_cast<ShaderGl>(drawable.material->getShader())->setMat4("engine_Model.View", MATH::Matrix4f::Transpose(uboData.View));
			render->setUniformBuffer("engine_UBO", mEngineUbo);
			WEB_LOG("bound engine_UBO");
			render->setUniformBuffer("engine_Bones", mBoneUbo);
			WEB_LOG("bound engine_Bones");
			render->setStorageBuffer("engine_Lights", mLightSSBO);
			WEB_LOG("bound engine_Lights");

			//glBindBufferBase(GL_UNIFORM_BUFFER, 0, std::static_pointer_cast<UniformBufferGl>(mEngineUbo)->getId());


			//drawable.mesh->bind();
			//if (drawable.mesh->getIndexCount() > 0) {
			//	// EBO
			//	glDrawElements(GL_TRIANGLES, drawable.mesh->getIndexCount(), GL_UNSIGNED_INT, nullptr);
			//	
			//}
			//drawable.mesh->unbind();

			render->draw(drawable.mesh, PrimitiveMode::TRIANGLES, drawable.material->getGPUInstances());
			WEB_LOG("draw done");
			if (drawLogs > 0) {
				--drawLogs;
				const auto err = glGetError();
				IKIGAI_COUT("draw indices=" << (drawable.mesh ? drawable.mesh->getIndexCount() : 0)
					<< " verts=" << (drawable.mesh ? drawable.mesh->getVertexCount() : 0)
					<< " instances=" << drawable.material->getGPUInstances()
					<< " skin=" << (data.use == 1 ? 1 : 0)
					<< " glError=" << static_cast<unsigned>(err));
			}


			//std::static_pointer_cast<ShaderGl>(drawable.material->getShader())->unbind();
			drawable.material->unbind();
		}
	}

	void GameRenderer::renderScene(IKIGAI::SCENE_SYSTEM::Scene& scene, IKIGAI::ECS::CameraComponent& cameraComponent) {
		uboData.View = MATH::Matrix4f::Transpose(cameraComponent.getCamera().getViewMatrix());
		auto projection = cameraComponent.getCamera().getProjectionMatrix();
		if (DriverInterface::settings.backend == RenderSettings::Backend::VULKAN) {
			// The engine builds OpenGL projections (NDC z in [-1, 1]), Vulkan clips z to [0, 1]:
			// without this correction everything in the near half of the depth range is clipped away.
			// Y flip is already done by the negative viewport height in DriverVk::EnsureViewport.
			MATH::Matrix4f clipCorrection = MATH::Matrix4f::Identity;
			clipCorrection(2, 2) = 0.5f;
			clipCorrection(2, 3) = 0.5f;
			projection = clipCorrection * projection;
		}
		uboData.Projection = MATH::Matrix4f::Transpose(projection);
		uboData.ViewPos = cameraComponent.obj->getTransform()->getWorldPosition();
		uboData.Time = 1.0f;
		auto sz = mContext.window->getSize();
		uboData.ViewportSize = MATH::Vector2f(sz.x,  sz.y);
		uboData.FPS = 60.0f;
		uboData.FrameCount = 1;
		mEngineUbo->setData(uboData);

		if (cameraComponent.isFrustumLightCulling()) {
			updateLightsInFrustum(scene, cameraComponent.getCamera().getFrustum());
		}
		else {
			updateLights(scene);
		}

		const auto& cameraPosition = cameraComponent.obj->getTransform()->getWorldPosition();
		auto chunks = scene.findDrawables(cameraPosition, cameraComponent.getCamera(), nullptr, mEmptyMaterial);

		static int sceneLogs = 4;
		if (sceneLogs > 0) {
			--sceneLogs;
			size_t opaque = 0;
			size_t transparent = 0;
			for (const auto& chunk : chunks) {
				opaque += chunk.opaqueDrawablesForward.size();
				transparent += chunk.transparentDrawablesForward.size();
			}
			const auto windowSize = mContext.window->getSize();
			GLint viewport[4] = {};
			glGetIntegerv(GL_VIEWPORT, viewport);
			const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
			GLint scissorBox[4] = {};
			if (scissor) {
				glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
			}
			IKIGAI_COUT("renderScene chunks=" << chunks.size()
				<< " opaque=" << opaque
				<< " transparent=" << transparent
				<< " cam=" << cameraPosition.x << "," << cameraPosition.y << "," << cameraPosition.z
				<< " window=" << windowSize.x << "x" << windowSize.y
				<< " viewport=" << viewport[2] << "x" << viewport[3]
				<< " scissor=" << (scissor ? 1 : 0)
				<< " scissorBox=" << scissorBox[2] << "x" << scissorBox[3]);
		}

		auto runStage = [&](std::unique_ptr<PipelineStage>& stage) {
			auto& render = RESOURCES::ServiceManager::Get<RENDER::Renderer>();
			
			for (auto& chunk : chunks) {
				if (chunk.frameBuffer) {
					render.setFrameBuffer(chunk.frameBuffer);
				}
				else if (stage->mFrameBuffer) {
					render.setFrameBuffer(stage->mFrameBuffer);
				}
				else {
					render.setFrameBuffer(nullptr);
				}

				render.setClearColor(1.0f, 0.0f, 0.0f, 1.0f);
				render.clear(true, true, false);

				if (stage->mMaterial) {
					stage->mMaterial->bind(mEmptyTexture, true);
					//fillUniforms(stage);
				}

				switch (stage->mDrawContent) {
					case DrawContent::FORWARD:
					{
						for (const auto& [distance, drawable] : chunk.opaqueDrawablesForward) {
							drawDrawable(drawable);
						}
						for (const auto& [distance, drawable] : chunk.transparentDrawablesForward) {
							drawDrawable(drawable);
						}
					}
					break;
					case DrawContent::DEFERRED:
					{
						for (const auto& [distance, drawable] : chunk.opaqueDrawablesDeferred) {
							drawDrawable(drawable);
						}
						for (const auto& [distance, drawable] : chunk.transparentDrawablesDeferred) {
							drawDrawable(drawable);
						}
					}
					break;
					case DrawContent::GUI: break; //TODO:
					case DrawContent::QUAD:
					{
						//mDriver->draw(*quad->getMeshes()[0], PrimitiveMode::TRIANGLES, 1);
					}
					break;
					default: break;
				}

				if (stage->mMaterial) {
					stage->mMaterial->unbind();
				}

				if (chunk.frameBuffer || stage->mFrameBuffer) {
					render.setFrameBuffer(nullptr);
				}
			} // end for chunk
		};

		if (!mRenderPipeline->mIsInitialized) {
			for (auto& stage : mRenderPipeline->mStartStages) {
				runStage(stage);
			}
			mRenderPipeline->mIsInitialized = true;
		}

		for (auto& stage : mRenderPipeline->mStages) {
			runStage(stage);
		}
	}

	namespace {
#if defined(USING_GLES) && !defined(IKIGAI_GLES_HAS_SSBO)
		void PadLightsForEsUbo(std::vector<LightOGL>& lights) {
			if (lights.size() > MAX_LIGHTS) {
				lights.resize(MAX_LIGHTS);
			} else {
				lights.resize(MAX_LIGHTS, LightOGL::Inactive());
			}
		}
#endif
	}

	void GameRenderer::updateLights(SCENE_SYSTEM::Scene& scene) {
		auto lightMatrices = scene.findLightData();
#if defined(USING_GLES) && !defined(IKIGAI_GLES_HAS_SSBO)
		PadLightsForEsUbo(lightMatrices);
#endif
		mLightSSBO->setData(lightMatrices);
	}

	void GameRenderer::updateLightsInFrustum(SCENE_SYSTEM::Scene& scene, const Frustum& frustum) {
		auto lightMatrices = scene.findLightDataInFrustum(frustum);
#if defined(USING_GLES) && !defined(IKIGAI_GLES_HAS_SSBO)
		PadLightsForEsUbo(lightMatrices);
#endif
		mLightSSBO->setData(lightMatrices);
	}

	const RenderGraphPipeline& GameRenderer::getCurrentPipeline() const {
		if (!mRenderPipeline) {
			throw std::runtime_error("Render pipeline is not ready");
		}
		return *mRenderPipeline;
	}

	void GameRenderer::setPipeline(std::unique_ptr<RenderGraphPipeline>&& renderPipeline) {
		mRenderPipeline = std::move(renderPipeline);
	}

#ifdef OCULUS
	void GameRenderer::renderSceneOculus(
		XrCompositionLayerProjectionView& layerView, render_target_t& rtarget,
		XrPosef& stagePose, uint32_t viewID) {
		(void)stagePose;

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

		XrMatrix4x4f matP{};
		XrMatrix4x4f matV{};
		XrMatrix4x4f matC{};
		XrMatrix4x4f_CreateProjectionFov(&matP, GRAPHICS_OPENGL_ES, layerView.fov,
			cameraComp->getNear(), cameraComp->getFar());

		const XrVector3f scale = {1.0f, 1.0f, 1.0f};
		XrMatrix4x4f_CreateTranslationRotationScale(&matC, &layerView.pose.position,
			&layerView.pose.orientation, &scale);
		XrMatrix4x4f_InvertRigidBody(&matV, &matC);

		auto toMat4 = [](const XrMatrix4x4f& from) {
			MATH::Matrix4f to(from.m[0], from.m[1], from.m[2], from.m[3], from.m[4],
				from.m[5], from.m[6], from.m[7], from.m[8], from.m[9],
				from.m[10], from.m[11], from.m[12], from.m[13],
				from.m[14], from.m[15]);
			return MATH::Matrix4f::Transpose(to);
		};
		cameraComp->getCamera().cacheViewMatrix(toMat4(matV));
		cameraComp->getCamera().cacheProjectionMatrix(toMat4(matP));

		renderScene(scene, *cameraComp);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
#endif
} // namespace IKIGAI::RENDER

IKIGAI::RENDER::RenderGraphPipeline::RenderGraphPipeline(const Descriptor& descriptor, Renderer& renderer) {
	// for (const auto& bufferDesc : descriptor.Buffers) {
	//	if (bufferDesc.Type == BufferType::UNIFORM) {
	//		mUniformBuffers[bufferDesc.Name] =
	//renderer.createUniformBuffer(nullptr, bufferDesc.Size);
	//	}
	//	else if (bufferDesc.Type == BufferType::STORAGE) {
	//		mStorageBuffers[bufferDesc.Name] =
	//renderer.createStorageBuffer(nullptr, bufferDesc.Size, bufferDesc.Stride);
	//	}
	// }

	for (const auto& desc : descriptor.Textures) {
		mTextures[desc.Name] = RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().loadResource(desc.Path);
	}

	for (const auto& fb : descriptor.FrameBuffers) {
		std::vector<RESOURCES::ResourcePtr<TextureInterface>> attachments;
		for (const auto& texName : fb.Textures) {
			if (mTextures.contains(texName)) {
				attachments.push_back(mTextures[texName]);
			} else {
			}
		}
		RESOURCES::ResourcePtr<TextureInterface> depthTex = nullptr;
		if (!fb.Depth.empty() && mTextures.contains(fb.Depth)) {
			depthTex = mTextures[fb.Depth];
		}
		mFrameBuffers[fb.Name] = renderer.createFrameBuffer(attachments, depthTex);
	}

	mStartStages = loadStages(descriptor.StartStages);
	mStages = loadStages(descriptor.Stages);
}

IKIGAI::RENDER::RenderGraphPipeline::StagesArr IKIGAI::RENDER::RenderGraphPipeline::loadStages(const std::vector<PipelineStage::Descriptor>& stages) {
	StagesArr result;

	for (const auto& stageDesc : stages) {
		auto stage = std::make_unique<PipelineStage>();
		stage->mName = stageDesc.Name;
		stage->mDrawContent = stageDesc.Draw;
		if (mFrameBuffers.contains(stageDesc.FrameBuffer)) {
			stage->mFrameBuffer = mFrameBuffers[stageDesc.FrameBuffer];
		}

		if (!stageDesc.Material.empty()) {
			stage->mMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(stageDesc.Material);
		}

		// Uniforms
		for (auto& u : stageDesc.Uniforms) {
			PipelineStage::UniformType val;
			std::visit([&val, this](auto&& arg) {
					using T = std::decay_t<decltype(arg)>;
					if constexpr (std::is_same_v<T, std::string>) {
						if (mTextures.contains(arg)) {
							val = mTextures[arg];
						}
					} else {
						val = arg;
					}
				},
				u.second);
			stage->mUniforms[u.first] = val;
		}

		// stage->mBufferLinks = stageDesc.BufferLinks;

		// Overrides
		// for (auto& pair : stageDesc.BufferOverrides) {
		//	for (auto& valPair : pair.second) {
		//		PipelineStage::UniformType val;
		//		std::visit([&val, this](auto&& arg) {
		//			using T = std::decay_t<decltype(arg)>;
		//			if constexpr (std::is_same_v<T, std::string>) {
		//
		//			} else {
		//				val = arg;
		//			}
		//			}, valPair.second);
		//		stage->mBufferOverrides[pair.first][valPair.first] = val;
		//	}
		//}

		result.push_back(std::move(stage));
	}

	return result;
}

void IKIGAI::RENDER::RenderGraphPipeline::run() {
	auto runStage = [&](std::unique_ptr<PipelineStage>& stage) {
		//auto& render = RESOURCES::ServiceManager::Get<RENDER::Renderer>();
		//if (stage->mFrameBuffer) {
		//	render.setFrameBuffer(stage->mFrameBuffer);
		//}
		//
		//render.setClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		//render.clear(true, true, false);
		//
		//if (stage->mMaterial) {
		//	stage->mMaterial->bind(mEmptyTexture, true);
		//	fillUniforms(stage);
		//}
		//
		//switch (stage->mDrawContent) {
		//case DrawContent::FORWARD:
		//{
		//	for (const auto& [distance, drawable] : mOpaqueMeshesForward) {
		//		drawDrawable(drawable);
		//	}
		//	for (const auto& [distance, drawable] : mTransparentMeshesForward) {
		//		drawDrawable(drawable);
		//	}
		//}
		//break;
		//case DrawContent::DEFERRED:
		//{
		//	for (const auto& [distance, drawable] : mOpaqueMeshesDeferred) {
		//		drawDrawable(drawable);
		//	}
		//	for (const auto& [distance, drawable] : mTransparentMeshesDeferred) {
		//		drawDrawable(drawable);
		//	}
		//}
		//break;
		//case DrawContent::GUI: break; //TODO:
		//case DrawContent::QUAD:
		//{
		//	//mDriver->draw(*quad->getMeshes()[0], PrimitiveMode::TRIANGLES, 1);
		//}
		//break;
		//default: break;
		//}
		//
		//if (stage->mMaterial) {
		//	stage->mMaterial->unbind();
		//}
		//
		//if (stage->mFrameBuffer) {
		//	render.setFrameBuffer(nullptr);
		//}
	};

	if (!mIsInitialized) {
		for (auto& stage : mStartStages) {
			runStage(stage);
		}
		mIsInitialized = true;
	}

	for (auto& stage : mStages) {
		runStage(stage);
	}
}
