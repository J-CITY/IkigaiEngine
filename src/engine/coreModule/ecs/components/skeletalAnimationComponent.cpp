#include "skeletalAnimationComponent.h"
#include "skeletalComponent.h"
#include "coreModule/ecs/object.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/skeletonAnimationManager.h"
#include "resourceModule/skeletonStateGraphManager.h"
#include "skeletalModule/animationInstance.h"
#include "skeletalModule/skeletalStateGraph.h"
#ifdef __EMSCRIPTEN__
#include <iostream>
#endif

namespace IKIGAI::ECS {
	SkeletalAnimationComponent::SkeletalAnimationComponent(UTILS::Ref<ECS::Object> obj) : ComponentBase(obj) {
		__NAME__ = "SkeletalAnimationComponent";
		mEnvironment = std::make_shared<UTILS::Environment>();
	}

	SkeletalAnimationComponent::SkeletalAnimationComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& _descriptor) : ComponentBase(obj) {
		__NAME__ = "SkeletalAnimationComponent";
		mEnvironment = std::make_shared<UTILS::Environment>();
		//TODO: path could be state graph or blending
		setAnimation(_descriptor.SkeletalPlayablePath);
	}

	SkeletalAnimationComponent::~SkeletalAnimationComponent() = default;

	void SkeletalAnimationComponent::onUpdate(std::chrono::duration<double> dt) {
		if (!mPlayable && !mGraphPath.empty()) {
			auto skelComp = obj->getComponent<SkeletalComponent>();
			if (skelComp && skelComp->getSkeleton()) {
				setAnimation(mGraphPath);
			}
		}
		if (mPlayable) {
			mPlayable->update(static_cast<float>(dt.count()));
		}
	}

	void SkeletalAnimationComponent::setPlayable(std::shared_ptr<SKELETON::IAnimationPlayable> playable) {
		mPlayable = playable;
	}

	void SkeletalAnimationComponent::setAnimation(const std::string& path) {
		mGraphPath = path;
		if (!path.empty()) {
			//TODO: res calculate file or resource
			auto res = RESOURCES::ServiceManager::Get<RESOURCES::SkeletonAnimationLoader>().loadResource(path);
#ifdef __EMSCRIPTEN__
			std::cout << "[web] skeletal anim load " << path << " res=" << (res ? "ok" : "null") << std::endl;
			std::cout.flush();
#endif
			if (res) {
				auto skelComp = obj->getComponent<SkeletalComponent>();
				std::shared_ptr<SKELETON::Skeleton> skeleton = nullptr;
				if (skelComp && skelComp->getSkeleton()) {
					skeleton = skelComp->getSkeleton();
				}
#ifdef __EMSCRIPTEN__
				std::cout << "[web] skeletal skeleton=" << (skeleton ? "ok" : "null") << std::endl;
				std::cout.flush();
#endif
				if (!skeleton) {
					mPlayable = nullptr;
					return;
				}
				mPlayable = std::make_shared<SKELETON::AnimSample>(skeleton, res);
			}
		} else {
			mPlayable = nullptr;
		}
	}

	void SkeletalAnimationComponent::setStateGraph(const std::string& path) {
		mGraphPath = path;
		if (!path.empty()) {
			auto graphRes = RESOURCES::ServiceManager::Get<RESOURCES::SkeletonStateGraphLoader>().loadResource(path);
			if (graphRes) {
				auto skelComp = obj->getComponent<SkeletalComponent>();
				std::shared_ptr<SKELETON::Skeleton> skeleton = nullptr;
				if (skelComp && skelComp->getSkeleton()) {
					skeleton = skelComp->getSkeleton();
				}
				
				mPlayable = std::make_shared<SKELETON::SkeletalStateGraphInstance>(skeleton, graphRes, mEnvironment);
			}
		} else {
			mPlayable = nullptr;
		}
	}

	SkeletalAnimationComponent::Descriptor SkeletalAnimationComponent::getDescriptor() const {
		Descriptor desc;
		desc.Type = GetComponentName<SkeletalAnimationComponent>();
		desc.SkeletalPlayablePath = mGraphPath;
		return desc;
	}

	void SkeletalAnimationComponent::onDeserialize(nlohmann::json& j) {
		if (j.contains("GraphPath")) {
			setStateGraph(j["GraphPath"].get<std::string>());
		}
	}

	void SkeletalAnimationComponent::onSerialize(nlohmann::json& j) {
		j["GraphPath"] = mGraphPath;
	}
}
