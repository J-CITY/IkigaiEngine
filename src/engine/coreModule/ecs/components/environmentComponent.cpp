#include "environmentComponent.h"
#include "coreModule/ecs/componentReference.inl"
#include "directionalLight.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ECS {
EnvironmentComponent::EnvironmentComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "EnvironmentComponent";
	mDescriptor.Type = GetType<EnvironmentComponent>();
	mPresetTransition.reset(mDescriptor.Environment);
	mRuntime.update(mDescriptor.Environment, 0);
}
EnvironmentComponent::EnvironmentComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: EnvironmentComponent(object) {
	mDescriptor = static_cast<const Descriptor&>(descriptor);
	mDescriptor.Environment.validate();
	mDescriptor.Quality.validate();
	mDescriptor.Environment = ENVIRONMENT::presetSettings(mDescriptor.Preset, mDescriptor.Environment);
	mPresetTransition.reset(mDescriptor.Environment);
	mRuntime.update(mDescriptor.Environment, 0);
}
EnvironmentComponent::Descriptor EnvironmentComponent::getDescriptor() const {
	return mDescriptor;
}
void EnvironmentComponent::apply(const ENVIRONMENT::Settings& value) {
	value.validate();
	mDescriptor.Environment = value;
	mDescriptor.Preset = ENVIRONMENT::Preset::Custom;
	mPresetTransition.reset(value);
	mRuntime.update(value, 0);
}
void EnvironmentComponent::applyPreset(ENVIRONMENT::Preset preset, float seconds) {
	auto target = ENVIRONMENT::presetSettings(preset, mPresetTransition.current());
	target.TransitionSeconds = seconds;
	mPresetTransition.start(target, seconds);
	mDescriptor.Environment = target;
	mDescriptor.Preset = preset;
	mRuntime.update(mPresetTransition.current(), 0);
}
ENVIRONMENT::Preset EnvironmentComponent::preset() const {
	return mDescriptor.Preset;
}
float EnvironmentComponent::presetProgress() const {
	return mPresetTransition.progress();
}
const ENVIRONMENT::Settings& EnvironmentComponent::effectiveSettings() const {
	return mPresetTransition.current();
}
ENVIRONMENT::Settings EnvironmentComponent::simulationSettings() const {
	return mDescriptor.Quality.limit(mPresetTransition.current());
}
const ENVIRONMENT::QualitySettings& EnvironmentComponent::quality() const {
	return mDescriptor.Quality;
}
void EnvironmentComponent::applyQuality(const ENVIRONMENT::QualitySettings& quality) {
	quality.validate();
	mDescriptor.Quality = quality;
}
const ENVIRONMENT::Settings& EnvironmentComponent::settings() const {
	return mDescriptor.Environment;
}
int EnvironmentComponent::sunObjectId() const {
	return mSun ? mSun->obj->getIDInt() : -1;
}
ENVIRONMENT::Runtime& EnvironmentComponent::runtime() {
	return mRuntime;
}
ENVIRONMENT::WeatherRuntime& EnvironmentComponent::weather() {
	return mWeather;
}
ENVIRONMENT::SurfaceWeatherRuntime& EnvironmentComponent::surfaceWeather() {
	return mSurfaceWeather;
}
void EnvironmentComponent::onStart() {
	mSun = {};
}
void EnvironmentComponent::update(SCENE_SYSTEM::Scene& scene, double dt) {
	if (!mDescriptor.Sun.empty() && !mSun) {
		mSun = Resolve<DirectionalLight>(scene, mDescriptor.Sun);
		if (!mSun) {
			throw std::runtime_error("Environment.Sun must reference an existing DirectionalLight");
		}
	}
	mPresetTransition.update(dt);
	mRuntime.update(mPresetTransition.current(), dt);
	if (mSun) {
		const auto& gpu = mRuntime.gpu();
		mSun->setColor({ gpu.SunColor.x, gpu.SunColor.y, gpu.SunColor.z });
		mSun->setIntensity(gpu.SunDirection.w);
		const MATH::Vector3f direction { -gpu.SunDirection.x, -gpu.SunDirection.y, -gpu.SunDirection.z };
		const float yaw = std::atan2(direction.x, direction.z);
		const float pitch = -std::asin(std::clamp(direction.y, -1.0f, 1.0f));
		auto rotation = MATH::QuaternionF(MATH::Vector3f { MATH::TO_DEGREES(pitch), MATH::TO_DEGREES(yaw), 0 });
		if (auto parent = mSun->obj->getParent()) {
			rotation = MATH::QuaternionF::Conjugate(parent->getTransform()->getWorldRotation()) * rotation;
		}
		mSun->obj->getTransform()->setLocalRotation(rotation);
	}
}
}
