#pragma once
#if USE_PHYSICS_3D
#include "component.h"
#include "physicsModule/vehicle.h"

namespace IKIGAI::ECS {
class VehicleComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor, PHYSICS::VehicleConfig, PHYSICS::DriftTuning {
		std::string Type = "VehicleComponent";
		std::string Profile = "Simulation";
		float Assistance = -1.f; // Legacy scenes select an endpoint through Profile.
		float HighSpeedSteering = 1.f;
		PHYSICS::SteeringMode Steering = PHYSICS::SteeringMode::AutoCenter;
		PHYSICS::TransmissionMode Transmission = PHYSICS::TransmissionMode::Direct;
		bool Keyboard = false;
		bool FitProvingGroundMeshes = false;
		template <class C> static void serde(C& context, Descriptor& value) {
			const PHYSICS::VehicleConfig defaults;
			const PHYSICS::DriftTuning driftDefaults;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "VehicleComponentType")
				.field(&Descriptor::Profile, "Profile", serde::attribute::default_ { std::string("Simulation") })
				.field(&Descriptor::FitProvingGroundMeshes, "FitProvingGroundMeshes", serde::attribute::default_ { false })
				.field(&Descriptor::antiSkidStrength, "AntiSkidStrength", serde::attribute::default_ { driftDefaults.antiSkidStrength })
				.field(&Descriptor::driftEnabled, "DriftEnabled", serde::attribute::default_ { driftDefaults.driftEnabled })
				.field(
					&Descriptor::breakawaySensitivity, "DriftBreakawaySensitivity", serde::attribute::default_ { driftDefaults.breakawaySensitivity })
				.field(&Descriptor::powerAngleDegrees, "DriftPowerAngleDegrees", serde::attribute::default_ { driftDefaults.powerAngleDegrees })
				.field(&Descriptor::recoverySeconds, "DriftRecoverySeconds", serde::attribute::default_ { driftDefaults.recoverySeconds })
				.field(&Descriptor::Assistance, "Assistance", serde::attribute::default_ { -1.f })
				.field(&Descriptor::HighSpeedSteering, "HighSpeedSteering", serde::attribute::default_ { 1.f })
				.field(&Descriptor::Steering, "Steering", serde::attribute::default_ { PHYSICS::SteeringMode::AutoCenter })
				.field(&Descriptor::Transmission, "Transmission", serde::attribute::default_ { PHYSICS::TransmissionMode::Direct })
				.field(&Descriptor::Keyboard, "Keyboard", serde::attribute::default_ { false })
				.field(&Descriptor::massOverrideKg, "MassOverrideKg", serde::attribute::default_ { defaults.massOverrideKg })
				.field(&Descriptor::chassis, "Chassis", serde::attribute::default_ { defaults.chassis })
				.field(&Descriptor::suspension, "Suspension", serde::attribute::default_ { defaults.suspension })
				.field(&Descriptor::tires, "Tires", serde::attribute::default_ { defaults.tires })
				.field(&Descriptor::brakes, "Brakes", serde::attribute::default_ { defaults.brakes })
				.field(&Descriptor::engine, "Engine", serde::attribute::default_ { defaults.engine })
				.field(&Descriptor::drive, "Drive", serde::attribute::default_ { defaults.drive })
				.field(&Descriptor::centerOfMass, "CenterOfMass", serde::attribute::default_ { defaults.centerOfMass })
				.field(&Descriptor::wheelBase, "WheelBase", serde::attribute::default_ { defaults.wheelBase })
				.field(&Descriptor::trackWidth, "TrackWidth", serde::attribute::default_ { defaults.trackWidth })
				.field(&Descriptor::radius, "Radius", serde::attribute::default_ { defaults.radius })
				.field(&Descriptor::restLength, "RestLength", serde::attribute::default_ { defaults.restLength })
				.field(&Descriptor::maxCompression, "MaxCompression", serde::attribute::default_ { defaults.maxCompression })
				.field(&Descriptor::maxDroop, "MaxDroop", serde::attribute::default_ { defaults.maxDroop })
				.field(&Descriptor::stiffness, "Stiffness", serde::attribute::default_ { defaults.stiffness })
				.field(&Descriptor::compressionDamping, "CompressionDamping", serde::attribute::default_ { defaults.compressionDamping })
				.field(&Descriptor::reboundDamping, "ReboundDamping", serde::attribute::default_ { defaults.reboundDamping })
				.field(&Descriptor::antiRollStiffness, "AntiRollStiffness", serde::attribute::default_ { defaults.antiRollStiffness })
				.field(&Descriptor::maxSuspensionForce, "MaxSuspensionForce", serde::attribute::default_ { defaults.maxSuspensionForce })
				.field(&Descriptor::driveTorque, "DriveTorque", serde::attribute::default_ { defaults.driveTorque });
		}
	};
	explicit VehicleComponent(UTILS::Ref<Object> owner);
	VehicleComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor& descriptor);
	const Descriptor& getDescriptor() const;
	void configure(const PHYSICS::VehicleConfig& settings);
	void onStart() override;
	void onUpdate(std::chrono::duration<double>) override;
	void present(const PHYSICS::Vehicle& vehicle);
	const PHYSICS::VehicleConfig& config() const;
	void setHighSpeedSteering(float multiplier);
	void setDriftTuning(const PHYSICS::DriftTuning& tuning);
	void setControls(float assistance, PHYSICS::SteeringMode steeringMode, PHYSICS::TransmissionMode transmission);
	int gear() const {
		return mGear;
	}
	bool nitroRequested = false;
	float handbrake = 0;
	float throttle = 0;
	float brake = 0;
	float steering = 0;
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	Descriptor mSettings {};
	int mGear = 1;
	std::weak_ptr<Object> mChassisVisual {};
	std::weak_ptr<Object> mCabinVisual {};
	std::array<std::weak_ptr<Object>, 4> mTireEffects {};
	std::array<std::weak_ptr<Object>, 2> mExhaustEffects {};
	std::weak_ptr<Object> mIdleAudio {};
	std::weak_ptr<Object> mLoadAudio {};
	std::array<std::weak_ptr<Object>, 4> mWheelVisuals {};
};
template <> inline std::string GetType<VehicleComponent>() {
	return "class IKIGAI::ECS::VehicleComponent";
}
template <> inline std::string GetComponentName<VehicleComponent>() {
	return "VehicleComponent";
}
}
#endif
