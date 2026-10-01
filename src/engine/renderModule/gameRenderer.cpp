#include "gameRenderer.h"


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
#include "renderModule/backends/interface/driverInterface.h"
#include "skeletalModule/animationOffset.h"
#include "skeletalModule/animationTransform.h"
#include "skeletalModule/iAnimationPlayable.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/time/time.h"
#include <stdexcept>
#include "windowModule/window/window.h"

#ifdef OCULUS
#include "util_matrix.h"
#include <common/xr_linear.h>
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

		mLightSSBO = render->createStorageBuffer(nullptr, 0, 0);
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

	void GameRenderer::renderScene() {
		auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::SCENE_SYSTEM::SceneManager>().getCurrentScene();
		auto& render = mContext.render;

		auto camera = scene.findMainCamera();

		if (auto mainCameraComponent = mContext.sceneManager->getCurrentScene().findMainCamera()) {
			// TODO: not use screen size
			auto sz = mContext.window->getSize();
			auto winWidth = sz.x;
			auto winHeight = sz.y;

			const auto& transform = mainCameraComponent->obj->getTransform();
			const auto& cameraPosition = transform->getWorldPosition();
			const auto& cameraRotation = transform->getWorldRotation();
			mainCameraComponent->getCamera().cacheMatrices(winWidth, winHeight, cameraPosition, cameraRotation);

			//const auto glState = mDriver->fetchGLState();
			renderScene(scene, *mainCameraComponent);
			//mDriver->applyStateMask(glState);
		}
		else {
			render->setClearColor(1.0f, 0.0f, 0.0f, 1.0f);
			render->clear(true, true, false);
		}
		mFrameCount++;
	}

	void GameRenderer::drawDrawable(const Drawable& drawable) {
		if (drawable.material->hasShader() && drawable.material->getGPUInstances() > 0) {
			auto& render = mContext.render;

			render->setDepth(drawable.material->getDepthFunc());
			//render->setCull(CullFace::NONE);

			render->setShader(drawable.material->getShader());
			drawable.material->bind(mEmptyTexture, true);

			//TODO: use render for it
			render->setPushConstant(IKIGAI::RENDER::ShaderType::VERTEX, 0, sizeof(MATH::Matrix4f), &drawable.world);

			BonesUBO data;
			if (drawable.skeleton && drawable.animationPlayable) {
				data.use = 1;
				{
					drawable.animationPlayable->update(0.0016f);
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
			mBoneUbo->setData(data);

			//std::static_pointer_cast<ShaderGl>(drawable.material->getShader())->setMat4("engine_Model.Projection", MATH::Matrix4f::Transpose(uboData.Projection));
			//std::static_pointer_cast<ShaderGl>(drawable.material->getShader())->setMat4("engine_Model.View", MATH::Matrix4f::Transpose(uboData.View));
			render->setUniformBuffer("engine_UBO", mEngineUbo);
			render->setUniformBuffer("engine_Bones", mBoneUbo);
			render->setStorageBuffer("engine_Lights", mLightSSBO);

			//glBindBufferBase(GL_UNIFORM_BUFFER, 0, std::static_pointer_cast<UniformBufferGl>(mEngineUbo)->getId());


			//drawable.mesh->bind();
			//if (drawable.mesh->getIndexCount() > 0) {
			//	// EBO
			//	glDrawElements(GL_TRIANGLES, drawable.mesh->getIndexCount(), GL_UNSIGNED_INT, nullptr);
			//	
			//}
			//drawable.mesh->unbind();

			render->draw(drawable.mesh, PrimitiveMode::TRIANGLES, drawable.material->getGPUInstances());


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

	void GameRenderer::updateLights(SCENE_SYSTEM::Scene& scene) {
		auto lightMatrices = scene.findLightData();
		mLightSSBO->setData(lightMatrices);
	}

	void GameRenderer::updateLightsInFrustum(SCENE_SYSTEM::Scene& scene, const Frustum& frustum) {
		auto lightMatrices = scene.findLightDataInFrustum(frustum);
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

		auto& scene = IKIGAI::RESOURCES::ServiceManager::Get<
			IKIGAI::SCENE_SYSTEM::SceneManager>()
			.getCurrentScene();
		auto& window =
			IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::WINDOW::Window>();

		mainCameraComponent = std::nullopt;
		if (mContext.sceneManager->hasCurrentScene()) {
			mainCameraComponent =
				mContext.sceneManager->getCurrentScene().findMainCamera();
		}
		if (!mainCameraComponent) {
			return;
		}
		auto cameraComp = mainCameraComponent.value();

		int view_x = layerView.subImage.imageRect.offset.x;
		int view_y = layerView.subImage.imageRect.offset.y;
		int view_w = layerView.subImage.imageRect.extent.width;
		int view_h = layerView.subImage.imageRect.extent.height;
		window.setSize(view_w, view_h);

		glBindFramebuffer(GL_FRAMEBUFFER, rtarget.fbo_id);

		glViewport(view_x, view_y, view_w, view_h);

		// mDriver->setClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		// mDriver->clear(true, true, false);
		// glClearColor (0.1f, 0.1f, 0.1f, 1.0f);
		// glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		/* ------------------------------------------- *
		 *  Matrix Setup
		 *    (matPV)  = (proj) x (view)
		 *    (matPVM) = (proj) x (view) x (model)
		 * ------------------------------------------- */
		XrMatrix4x4f matP, matV, matC, matM, matPV, matPVM;

		/* Projection Matrix */
		XrMatrix4x4f_CreateProjectionFov(&matP, GRAPHICS_OPENGL_ES, layerView.fov,
			cameraComp->getNear(), cameraComp->getFar());

		/* View Matrix (inverse of Camera matrix) */
		XrVector3f scale = {1.0f, 1.0f, 1.0f};
		const auto& vewPose = layerView.pose;
		XrMatrix4x4f_CreateTranslationRotationScale(&matC, &vewPose.position,
			&vewPose.orientation, &scale);
		XrMatrix4x4f_InvertRigidBody(&matV, &matC);

		/* Stage Space Matrix */
		XrMatrix4x4f_CreateTranslationRotationScale(&matM, &stagePose.position,
			&stagePose.orientation, &scale);

		XrMatrix4x4f_Multiply(&matPV, &matP, &matV);
		XrMatrix4x4f_Multiply(&matPVM, &matPV, &matM);

		mainCameraComponent.value()->obj->getTransform()->setLocalPosition(
			MATH::Vector3f(stagePose.position.x, stagePose.position.y,
			stagePose.position.z));
		mainCameraComponent.value()->obj->getTransform()->setLocalRotation(
			MATH::QuaternionF(stagePose.orientation.x, stagePose.orientation.y,
			stagePose.orientation.z, stagePose.orientation.w));

		auto toMat4 = [](const XrMatrix4x4f& from) {
			MATH::Matrix4f to(from.m[0], from.m[1], from.m[2], from.m[3], from.m[4],
				from.m[5], from.m[6], from.m[7], from.m[8], from.m[9],
				from.m[10], from.m[11], from.m[12], from.m[13],
				from.m[14], from.m[15]);
			return MATH::Matrix4f::Transpose(to);
			};
		mainCameraComponent.value()->getCamera().cacheViewMatrix(toMat4(matV));
		mainCameraComponent.value()->getCamera().cacheProjectionMatrix(toMat4(matP));

		// cameraComp->setFov(layerView.fov.);
		// cameraComp->setNear(0.05f);
		// cameraComp->setFar(100.0f);

		/* ------------------------------------------- *
		 *  Render
		 * ------------------------------------------- */
		 // float *matStage = reinterpret_cast<float*>(&matPVM);

		renderScene(mainCameraComponent.value());
		// draw_stage (matStage);
		// draw_triangle (matStage);

		//{
		//	XrVector3f    &pos = layerView.pose.position;
		//	XrQuaternionf &rot = layerView.pose.orientation;
		//	XrFovf        &fov = layerView.fov;
		//	int x = 100;
		//	int y = 100;
		//	char strbuf[128];
		//	update_dbgstr_winsize (view_w, view_h);
		//	sprintf (strbuf, "VIEWPOS(%6.4f, %6.4f, %6.4f)", pos.x, pos.y, pos.z);
		//	draw_dbgstr(strbuf, x, y); y += 22;
		//	sprintf (strbuf, "VIEWROT(%6.4f, %6.4f, %6.4f, %6.4f)", rot.x, rot.y,
		// rot.z, rot.w); 	draw_dbgstr(strbuf, x, y); y += 22; 	sprintf (strbuf,
		//"VIEWFOV(%6.4f, %6.4f, %6.4f, %6.4f)", 			 fov.angleLeft,
		// fov.angleRight, fov.angleUp, fov.angleDown); 	draw_dbgstr(strbuf, x,
		// y); y += 22;
		//}

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
