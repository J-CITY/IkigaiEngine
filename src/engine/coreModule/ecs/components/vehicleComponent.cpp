#include "vehicleComponent.h"
#if USE_PHYSICS_3D
#include "audioComponent.h"
#include "coreModule/ecs/componentCatalog.h"
#include "particleSystemComponent.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/inputManager/inputManager.h"
#include "windowModule/window/window.h"
#include <numbers>

namespace IKIGAI::ECS {
namespace {
	const bool registered = ComponentCatalog::enroll<VehicleComponent>();
}
VehicleComponent::VehicleComponent(UTILS::Ref<Object> owner)
	: VehicleComponent(owner, Descriptor {}) { }
VehicleComponent::VehicleComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor& descriptor)
	: ComponentBase(owner)
	, mSettings(static_cast<const Descriptor&>(descriptor)) {
	__NAME__ = GetComponentName<VehicleComponent>();
	mSettings.VehicleConfig::validate();
	mSettings.DriftTuning::validate();
	setHighSpeedSteering(mSettings.HighSpeedSteering);
	if (mSettings.Profile != "Casual" && mSettings.Profile != "Simulation") {
		throw std::invalid_argument("Unknown vehicle profile: " + mSettings.Profile);
	}
	setControls(mSettings.Assistance == -1.f ? (mSettings.Profile == "Casual" ? 1.f : 0.f) : mSettings.Assistance, mSettings.Steering,
		mSettings.Transmission);
}
void VehicleComponent::setHighSpeedSteering(float multiplier) {
	if (!(multiplier >= PHYSICS::Vehicle::minHighSpeedSteering && multiplier <= PHYSICS::Vehicle::maxHighSpeedSteering)) {
		throw std::invalid_argument("Invalid high-speed steering multiplier");
	}
	mSettings.HighSpeedSteering = multiplier;
}
void VehicleComponent::setDriftTuning(const PHYSICS::DriftTuning& tuning) {
	tuning.validate();
	static_cast<PHYSICS::DriftTuning&>(mSettings) = tuning;
}
void VehicleComponent::setControls(float assistance, PHYSICS::SteeringMode steeringMode, PHYSICS::TransmissionMode transmission) {
	if (!(assistance >= 0 && assistance <= 1) || steeringMode < PHYSICS::SteeringMode::AutoCenter || steeringMode > PHYSICS::SteeringMode::HoldAngle
		|| transmission < PHYSICS::TransmissionMode::Direct || transmission > PHYSICS::TransmissionMode::ForwardReverse) {
		throw std::invalid_argument("Invalid vehicle controls");
	}
	if (mSettings.Transmission != transmission) {
		mGear = 1;
		throttle = 0;
	}
	mSettings.Assistance = assistance;
	mSettings.Profile = assistance == 1.f ? "Casual" : "Simulation";
	mSettings.Steering = steeringMode;
	mSettings.Transmission = transmission;
}
const VehicleComponent::Descriptor& VehicleComponent::getDescriptor() const {
	return mSettings;
}
const PHYSICS::VehicleConfig& VehicleComponent::config() const {
	return mSettings;
}
void VehicleComponent::configure(const PHYSICS::VehicleConfig& settings) {
	settings.validate();
	static_cast<PHYSICS::VehicleConfig&>(mSettings) = settings;
}
void VehicleComponent::onStart() {
	mChassisVisual = obj->findUniqueChild("Chassis");
	mCabinVisual = obj->findUniqueChild("Cabin");
	mIdleAudio = obj->findUniqueChild("Engine idle");
	mLoadAudio = obj->findUniqueChild("Engine load");
	mExhaustEffects[0] = obj->findUniqueChild("Left exhaust");
	mExhaustEffects[1] = obj->findUniqueChild("Right exhaust");
	constexpr std::array tireNames { "Front left spray", "Front right spray", "Rear left spray", "Rear right spray" };
	constexpr std::array names { "FrontLeft", "FrontRight", "RearLeft", "RearRight" };
	for (size_t i = 0; i < names.size(); ++i) {
		mWheelVisuals[i] = obj->findUniqueChild(names[i]);
		mTireEffects[i] = obj->findUniqueChild(tireNames[i]);
	}
}
void VehicleComponent::onUpdate(std::chrono::duration<double>) {
	if (!mSettings.Keyboard) {
		return;
	}
	auto& input = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>();
	using Key = INPUT_SYSTEM::EKey;
	nitroRequested = false;
	throttle = steering = brake = handbrake = 0;
	if (input.getUiCapture().keyboard || !RESOURCES::ServiceManager::Get<WINDOW::Window>().hasFocus()) {
		return;
	}
	nitroRequested = input.isKeyPressed(Key::KEY_LEFT_SHIFT);
	throttle = float(input.isKeyPressed(Key::KEY_W));
	if (mSettings.Transmission == PHYSICS::TransmissionMode::Direct) {
		throttle -= float(input.isKeyPressed(Key::KEY_S));
	}
	else {
		const int shift = int(input.wasKeyPressedThisFrame(Key::KEY_E)) - int(input.wasKeyPressedThisFrame(Key::KEY_Q));
		if (shift != 0) {
			mGear = mSettings.Transmission == PHYSICS::TransmissionMode::Manual ? std::clamp(mGear + shift, -1, 5) : shift;
		}
	}
	handbrake = float(input.isKeyPressed(Key::KEY_SPACE));
	brake = float(input.isKeyPressed(Key::KEY_LEFT_CONTROL));
	steering = float(input.isKeyPressed(Key::KEY_A)) - float(input.isKeyPressed(Key::KEY_D));
	if (input.isKeyPressed(Key::KEY_C)) {
		setControls(1, mSettings.Steering, mSettings.Transmission);
	}
	if (input.isKeyPressed(Key::KEY_V)) {
		setControls(0, mSettings.Steering, mSettings.Transmission);
	}
}
void VehicleComponent::present(const PHYSICS::Vehicle& vehicle) {
	const auto h = vehicle.config.chassis == PHYSICS::ChassisStyle::Authored ? vehicle.originalShape.halfSize
																			 : PHYSICS::chassisDimensions(vehicle.config.chassis).halfSize;
	auto resize = [](auto& reference, MATH::Vector3f size) {
		if (auto visual = reference.lock(); visual && visual->tryGetTransform()->getLocalScale() != size) {
			visual->tryGetTransform()->setLocalScale(size);
		}
	};
	if (mSettings.FitProvingGroundMeshes) {
		resize(mChassisVisual, h * 2.f);
		resize(mCabinVisual, { h.x * 1.6f, h.y * 2.05f, h.z * (vehicle.config.chassis == PHYSICS::ChassisStyle::Truck ? .55f : .89f) });
		if (auto cabin = mCabinVisual.lock()) {
			const MATH::Vector3f pos { 0, h.y * 1.64f, h.z * (vehicle.config.chassis == PHYSICS::ChassisStyle::Truck ? .56f : -.0833f) };
			if (cabin->tryGetTransform()->getLocalPosition() != pos) {
				cabin->tryGetTransform()->setLocalPosition(pos);
			}
		}
	}
	const float load = std::abs(vehicle.throttle);
	const float wheelSpeed = .25f
		* (std::abs(vehicle.state.wheels[0].angularSpeed) + std::abs(vehicle.state.wheels[1].angularSpeed)
			+ std::abs(vehicle.state.wheels[2].angularSpeed) + std::abs(vehicle.state.wheels[3].angularSpeed));
	const float gearScale = vehicle.transmission == PHYSICS::TransmissionMode::Manual ? 1.f / std::max(1, vehicle.gear) : .63f;
	const float revs = std::clamp(.012f * wheelSpeed * gearScale + .23f * load + (vehicle.state.boosting ? .18f : 0.f), 0.f, 1.f);
	auto mixVoice = [](const std::weak_ptr<Object>& ref, float volume, float pitch) {
		if (auto source = ref.lock()) {
			if (auto audio = source->getComponent<AudioComponent>(); audio && audio->res && audio->res->id) {
				audio->setVolume(volume);
				AUDIO::AudioManagerAL::GetInstance().setPlaybackSpeed(*audio->res, pitch);
			}
		}
	};
	mixVoice(mIdleAudio, .42f - .24f * load, .92f + .85f * revs);
	mixVoice(mLoadAudio, .48f * load, .72f + .86f * revs);
	for (size_t i = 0; i < mExhaustEffects.size(); ++i) {
		if (auto nozzle = mExhaustEffects[i].lock()) {
			const MATH::Vector3f position { (i == 0 ? -1.f : 1.f) * h.x * .65f, -h.y * .65f, -h.z - .04f };
			if (nozzle->tryGetTransform()->getLocalPosition() != position) {
				nozzle->tryGetTransform()->setLocalPosition(position);
			}
			if (auto fx = nozzle->getComponent<ParticleSystem3DComponent>()) {
				fx->setRuntimeParameter("ExhaustRate", { load * (vehicle.state.boosting ? 6.f : 23.f) });
				fx->setRuntimeParameter("NitroRate", { vehicle.state.boosting ? 88.f : 0.f });
			}
		}
	}
	constexpr float degrees = 180.f / std::numbers::pi_v<float>;
	for (size_t i = 0; i < mWheelVisuals.size(); ++i) {
		if (auto contact = mTireEffects[i].lock()) {
			const auto& wheel = vehicle.state.wheels[i];
			auto position = vehicle.config.mount(i);
			position.y -= wheel.length + vehicle.config.radius - .06f;
			if (contact->tryGetTransform()->getLocalPosition() != position) {
				contact->tryGetTransform()->setLocalPosition(position);
			}
			if (auto fx = contact->getComponent<ParticleSystem3DComponent>()) {
				struct Spray {
					MATH::Vector3f color;
					float alpha;
					float radius;
					float density;
				};
				// Same order as RoadSurface. Dry pavement emits smoke only during slip.
				static const Spray surfaces[] { { { .47f, .47f, .47f }, .38f, .16f, 0 }, { { .48f, .48f, .48f }, .44f, .19f, 0 },
					{ { .69f, .88f, 1.f }, .52f, .07f, .60f }, { { .25f, .13f, .055f }, .90f, .13f, 1.9f },
					{ { .77f, .62f, .32f }, .62f, .20f, 1.8f }, { { .92f, .95f, 1.f }, .82f, .16f, 2.f }, { { .58f, .72f, .81f }, .43f, .10f, 2.1f },
					{ { .60f, .42f, .24f }, .61f, .17f, 1.5f } };
				const auto& spray = surfaces[static_cast<size_t>(wheel.surface)];
				const float rate = wheel.normalLoad > 0
					? std::min(38.f, std::max(0.f, vehicle.state.speed - .65f) * spray.density + 3.f * std::max(0.f, wheel.slipSpeed - 1.6f))
					: 0.f;
				fx->setRuntimeParameter("Rate", { rate });
				fx->setRuntimeParameter("Tint", { spray.color.x, spray.color.y, spray.color.z, spray.alpha });
				fx->setRuntimeParameter("Size", { spray.radius, spray.radius });
			}
		}
		if (auto visual = mWheelVisuals[i].lock()) {
			const auto& wheel = vehicle.state.wheels[i];
			const float radius = vehicle.config.radius;
			if (mSettings.FitProvingGroundMeshes) {
				resize(mWheelVisuals[i], { radius * .682f, radius * 2, radius * 2 });
			}
			auto center = vehicle.config.mount(i);
			center.y -= wheel.length;
			const MATH::QuaternionF steer(MATH::Vector3f { 0, wheel.steer * degrees, 0 });
			const MATH::QuaternionF spin(MATH::Vector3f { wheel.rotation * degrees, 0, 0 });
			visual->tryGetTransform()->setLocalPose(center, steer * spin);
		}
	}
}
}
#endif
