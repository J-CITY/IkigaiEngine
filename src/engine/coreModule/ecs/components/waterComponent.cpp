#include "waterComponent.h"
#include "coreModule/ecs/componentCatalog.h"
#include "materialRenderer.h"
#include "renderModule/render.h"
#include "utilsModule/serviceManager.h"
#include <cmath>
#include <numbers>
namespace IKIGAI::ECS {
namespace {
	constexpr float Tau = 2.f * std::numbers::pi_v<float>;
	bool between(float value, float low, float high) {
		return value >= low && value <= high;
	}
}
WaterComponent::WaterComponent(UTILS::Ref<Object> object)
	: WaterComponent(object, Descriptor {}) { }
WaterComponent::WaterComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(object) {
	__NAME__ = "WaterComponent";
	configure(static_cast<const Descriptor&>(descriptor));
}
void WaterComponent::configure(const Descriptor& s) {
	if (!between(s.Amplitude, 0, 2) || !between(s.Wavelength, 2, 40) || !between(s.WindSpeed, 0, 40) || !between(s.Heading, -180, 180)
		|| !between(s.Current, -3, 3) || !between(s.Roughness, .04f, 1) || !between(s.Foam, 0, 1) || !between(s.HullFoam, 0, 1)
		|| !between(s.Color.x, 0, 1) || !between(s.Color.y, 0, 1) || !between(s.Color.z, 0, 1)) {
		throw std::invalid_argument("Water parameters exceed supported ranges");
	}
	mSettings = s;
	constexpr std::array<float, 3> lengths { 1, .57f, .32f };
	constexpr std::array<float, 3> amplitudes { .62f, .26f, .12f };
	constexpr std::array<float, 3> angles { 0, .61f, -.83f };
	float heading = s.Heading * Tau / 360.f;
	mFlow = { std::cos(heading) * (s.Current + .012f * s.WindSpeed), 0, std::sin(heading) * (s.Current + .012f * s.WindSpeed) };
	for (size_t i = 0; i < mWaves.size(); ++i) {
		float k = Tau / (s.Wavelength * lengths[i]);
		mWaves[i] = { k * std::cos(heading + angles[i]), k * std::sin(heading + angles[i]), s.Amplitude * amplitudes[i], mWaves[i].w };
		mFrequency[i] = std::sqrt(9.81f * k) + mWaves[i].x * mFlow.x + mWaves[i].y * mFlow.z;
	}
}
void WaterComponent::advance(float dt) {
	for (size_t i = 0; i < mWaves.size(); ++i) {
		mWaves[i].w = std::remainder(mWaves[i].w - dt * mFrequency[i], Tau);
	}
}
float WaterComponent::displacement(float x, float z) const {
	float height = 0;
	for (const auto& wave : mWaves) {
		height += wave.z * std::sin(wave.x * x + wave.y * z + wave.w);
	}
	return height;
}
MATH::Vector3f WaterComponent::velocity(float x, float z) const {
	auto result = mFlow;
	for (size_t i = 0; i < mWaves.size(); ++i) {
		const auto& w = mWaves[i];
		result.y -= w.z * mFrequency[i] * std::cos(w.x * x + w.y * z + w.w);
	}
	return result;
}
void WaterComponent::present(const FoamHull& hull) {
	auto slots = obj->getComponent<MaterialRenderer>();
	if (!slots) {
		return;
	}
	auto source = slots->GetMaterialAtIndex(0);
	if (!source) {
		return;
	}
	if (source != mMaterial) {
		auto descriptor = source->getDescriptor();
		if (descriptor.ShaderPath != "/shaders/water/ocean.shader") {
			return;
		}
		descriptor.path = source->getPath();
		descriptor.NeedFileWatch = false;
		mMaterial = RESOURCES::ServiceManager::Get<RENDER::Renderer>().createMaterial(descriptor);
		mMaterial->useSurfaceGeometryShader(true);
		mMaterial->setIsCastShadow(false);
		for (unsigned i = 0; i < slots->getMaterials().size(); ++i) {
			if (slots->GetMaterialAtIndex(i) == source) {
				slots->setMaterial(i, mMaterial);
			}
		}
	}
	mMaterial->set("water_Hull", hull.footprint);
	mMaterial->set("water_HullMotion", hull.motion);
	mMaterial->set("water_PreviousPhases", mPresentedPhases);
	mPresentedPhases = { mWaves[0].w, mWaves[1].w, mWaves[2].w, 0 };
	mMaterial->set("water_WaveA", mWaves[0]);
	mMaterial->set("water_WaveB", mWaves[1]);
	mMaterial->set("water_WaveC", mWaves[2]);
	mMaterial->set("water_Tint", MATH::Vector4f { mSettings.Color.x, mSettings.Color.y, mSettings.Color.z, mSettings.Roughness });
	mMaterial->set("water_Weather", MATH::Vector4f { mSettings.WindSpeed, mSettings.Foam, mSettings.HullFoam, hull.agitation });
}
namespace {
	const bool registered = ComponentCatalog::enroll<WaterComponent>();
}
}
