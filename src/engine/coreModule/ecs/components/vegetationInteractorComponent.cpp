#include "vegetationInteractorComponent.h"
#include "coreModule/ecs/object.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ECS {
VegetationInteractorComponent::VegetationInteractorComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VegetationInteractorComponent";
	settings.Type = GetType<VegetationInteractorComponent>();
}
VegetationInteractorComponent::VegetationInteractorComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: VegetationInteractorComponent(object) {
	settings = static_cast<const Descriptor&>(descriptor);
	if (!std::isfinite(settings.Radius) || settings.Radius <= 0 || !std::isfinite(settings.Height) || settings.Height < 0
		|| !std::isfinite(settings.Strength) || settings.Strength < 0 || !std::isfinite(settings.RecoverySeconds) || settings.RecoverySeconds < 0) {
		throw std::invalid_argument("Invalid VegetationInteractor dimensions/strength/recovery");
	}
}
VegetationInteractorComponent::Descriptor VegetationInteractorComponent::getDescriptor() const {
	return settings;
}
void VegetationInteractorComponent::onUpdate(std::chrono::duration<double> elapsed) {
	const float dt = std::clamp(float(elapsed.count()), 0.0f, .25f);
	mPrevious = mSamples;
	mTrailTime += dt;
	const float interval = std::max(settings.RecoverySeconds / 3, .016f);
	if (mTrailTime >= interval) {
		for (int i = 3; i > 0; --i) {
			mSamples[i] = mSamples[i - 1];
		}
		mTrailTime = std::fmod(mTrailTime, interval);
	}
	for (size_t i = 1; i < mSamples.size(); ++i) {
		mSamples[i].EndStrength.w = std::max(0.0f, mSamples[i].EndStrength.w - dt * settings.Strength / std::max(settings.RecoverySeconds, .001f));
	}
	const auto matrix = obj->getTransform()->getWorldMatrix();
	const auto start = matrix * settings.Offset;
	const auto end = matrix * (settings.Offset + MATH::Vector3f { 0, settings.Height, 0 });
	mSamples[0] = { { start.x, start.y, start.z, settings.Radius }, { end.x, end.y, end.z, settings.Strength } };
	if (mPrevious[0].StartRadius.w == 0) {
		mPrevious = mSamples;
	}
}
void VegetationInteractorComponent::onDisable() {
	mSamples = {};
	mPrevious = {};
}
const std::array<VegetationInteractorComponent::Sample, 4>& VegetationInteractorComponent::samples() const {
	return mSamples;
}
const std::array<VegetationInteractorComponent::Sample, 4>& VegetationInteractorComponent::previousSamples() const {
	return mPrevious;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VegetationInteractorComponent>();
	return true;
}();
}
