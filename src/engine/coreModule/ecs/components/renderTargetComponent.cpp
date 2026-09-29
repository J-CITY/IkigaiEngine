#include "renderTargetComponent.h"

#include "renderModule/gameRenderer.h"
#include "renderModule/gameRendererInterface.h"
#include "sceneModule/sceneManager.h"

namespace IKIGAI::ECS {
	//TODO: add signal update frame buffer, when set render pipeline
	//TODO: add signal to subscribe on object with camera
	RenderTargetComponent::RenderTargetComponent(UTILS::Ref<ECS::Object> _obj) : ComponentBase(_obj) {
		__NAME__ = "RenderTargetComponent";

	}

	RenderTargetComponent::RenderTargetComponent(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor) : RenderTargetComponent(_obj) {
		mFlowType = descriptor.flowType;
		mFrameBufferName = descriptor.frameBufferName;
		mCameraObjectId = ECS2::Entity(ECS2::Entity::ID(descriptor.cameraObjectId));
	}

	void RenderTargetComponent::setName(const std::string& name) {
		mFrameBufferName = name;
	}

	const std::string& RenderTargetComponent::getTargetName() const {
		return mFrameBufferName;
	}

	RenderFlowType RenderTargetComponent::getType() const {
		return mFlowType;
	}

	std::shared_ptr<RENDER::FrameBufferInterface> RenderTargetComponent::getFrameBuffer() {
		auto& render = RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>();
		auto& pipeline = render.getCurrentPipeline();
		if (pipeline.mFrameBuffers.contains(mFrameBufferName)) {
			mFrameBuffer = pipeline.mFrameBuffers.at(mFrameBufferName);
		}
		else {
			mFrameBuffer = nullptr;
		}
		return mFrameBuffer;
	}

	UTILS::WeakPtr<CameraComponent> RenderTargetComponent::getCamera() {
		auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
		auto obj = scene.findObjectByID(mCameraObjectId);
		if (obj) {
			mCamera = obj->getComponent<CameraComponent>();
		}
		return mCamera;
	}

	RenderTargetComponent::Descriptor RenderTargetComponent::getDescriptor() const {
		Descriptor descriptor;
		descriptor.flowType = mFlowType;
		descriptor.frameBufferName = mFrameBufferName;
		descriptor.cameraObjectId = static_cast<int>(mCameraObjectId.getUniqueId());
		return descriptor;
	}
}
