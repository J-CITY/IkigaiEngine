#include "propertyAnimationComponent.h"

#include "coreModule/ecs/componentCatalog.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/log/logger.h"
#include "utilsModule/serialization/typedDescriptor.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

PropertyAnimationComponent::PropertyAnimationComponent(UTILS::Ref<ECS::Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "PropertyAnimationComponent";
}

PropertyAnimationComponent::PropertyAnimationComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ComponentBase(obj)
	, mTimelinePath(descriptor.TimelinePath)
	, mPlayOnStart(descriptor.PlayOnStart)
	, mLoopOverride(descriptor.LoopOverride)
	, mLoop(descriptor.Loop)
	, mTimelineEvents(descriptor.TimelineEvents) {
	__NAME__ = "PropertyAnimationComponent";
}

PropertyAnimationComponent::Descriptor PropertyAnimationComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<PropertyAnimationComponent>();
	descriptor.TimelinePath = mTimelinePath;
	descriptor.PlayOnStart = mPlayOnStart;
	descriptor.LoopOverride = mLoopOverride;
	descriptor.Loop = mLoop;
	descriptor.TimelineEvents = mTimelineEvents;
	return descriptor;
}

void PropertyAnimationComponent::rebuildRuntimeAnimation() {
	mAnimation.reset();
	mLoaded = false;
	if (mTimelinePath.empty()) {
		return;
	}

	auto& fileSystem = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	if (!fileSystem.isFileExist(mTimelinePath)) {
		LOG_WARNING << "PropertyAnimationComponent: timeline asset not found: " << mTimelinePath;
		return;
	}
	const auto file = fileSystem.getFile(mTimelinePath);
	if (!file) {
		LOG_WARNING << "PropertyAnimationComponent: failed to open timeline: " << mTimelinePath;
		return;
	}

	ANIMATION::TimelineAnimationDescriptor descriptor;
	try {
		descriptor = DATA::readDescriptor<ANIMATION::TimelineAnimationDescriptor>(nlohmann::json::parse(file->readStr()));
	} catch (const std::exception& ex) {
		LOG_ERROR << "PropertyAnimationComponent: invalid timeline JSON at " << mTimelinePath << ": " << ex.what();
		return;
	}

	mAnimation = ANIMATION::Animation::createFromDescriptor(descriptor);
	if (!mAnimation) {
		return;
	}
	if (mLoopOverride) {
		mAnimation->setLooped(mLoop);
	}
	mLoaded = true;
}

void PropertyAnimationComponent::onEnable() {
	rebuildRuntimeAnimation();
	if (mPlayOnStart && mAnimation) {
		mAnimation->play();
	}
}

void PropertyAnimationComponent::onDisable() {
	if (mAnimation) {
		mAnimation->stop();
	}
}

void PropertyAnimationComponent::play() {
	if (!mAnimation) {
		rebuildRuntimeAnimation();
	}
	if (mAnimation) {
		mAnimation->play();
	}
}

void PropertyAnimationComponent::pause() {
	if (mAnimation) {
		mAnimation->pause();
	}
}

void PropertyAnimationComponent::stop() {
	if (mAnimation) {
		mAnimation->stop();
	}
}

bool PropertyAnimationComponent::isPlaying() const {
	return mAnimation && mAnimation->getStatus() == ANIMATION::AnimationBase::Status::PLAY;
}

ANIMATION::Animation* PropertyAnimationComponent::getRuntimeAnimation() {
	return mAnimation.get();
}

const ANIMATION::Animation* PropertyAnimationComponent::getRuntimeAnimation() const {
	return mAnimation.get();
}

namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::PropertyAnimationComponent>();
	return true;
}();
}
