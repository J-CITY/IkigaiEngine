#pragma once
#include "collisionModule/collision3d.h"
#include "mathModule/math.h"
#include <array>
#include <cstddef>

namespace IKIGAI::PHYSICS {
class PhysicWorld;
struct Body;
enum class VehicleProfile { Simulation, Casual };
enum class SteeringMode { AutoCenter, HoldAngle };
enum class TransmissionMode { Direct, Manual, ForwardReverse };
enum class ChassisStyle { Authored, Sport, Crossover, Truck };
enum class SuspensionStyle { Authored, Sport, Touring, HeavyDuty };
enum class TireStyle { Road, Performance, AllTerrain, Winter };
enum class BrakeStyle { Standard, Sport, HeavyDuty };
enum class EngineStyle { Authored, Sport, Utility, Diesel };
enum class DriveLayout { Rear, Front, All };
enum class RoadSurface { Collider, Asphalt, Ice, Mud, Sand, Snow, WetAsphalt, DirtRoad };
struct ChassisDimensions {
	float mass = 0;
	MATH::Vector3f halfSize {};
};
ChassisDimensions chassisDimensions(ChassisStyle style);
struct RoadResponse {
	float grip = 1;
	float rollingResistance = 0;
};
RoadResponse tireResponse(TireStyle tire, RoadSurface surface, float colliderFriction);
struct VehicleConfig {
	ChassisStyle chassis = ChassisStyle::Authored;
	float massOverrideKg = 0; // Zero uses the selected chassis mass.
	SuspensionStyle suspension = SuspensionStyle::Authored;
	TireStyle tires = TireStyle::Road;
	BrakeStyle brakes = BrakeStyle::Standard;
	EngineStyle engine = EngineStyle::Authored;
	DriveLayout drive = DriveLayout::Rear;
	MATH::Vector3f centerOfMass {};
	bool operator==(const VehicleConfig&) const = default;
	VehicleConfig resolved() const;
	float wheelBase = 2.7f;
	float trackWidth = 1.6f;
	float radius = .33f;
	float restLength = .35f;
	float maxCompression = .15f;
	float maxDroop = .15f;
	float stiffness = 35000;
	float compressionDamping = 3000;
	float reboundDamping = 4500;
	float antiRollStiffness = 15000;
	float maxSuspensionForce = 15000;
	float driveTorque = 1200; // Total rear axle torque (Nm).
	void validate() const;
	MATH::Vector3f mount(std::size_t wheel) const;
};
VehicleConfig vehiclePreset(ChassisStyle style);
struct WheelState {
	float length = 0;
	float normalLoad = 0;
	float slipSpeed = 0;
	float angularSpeed = 0;
	float rotation = 0;
	float steer = 0;
	RoadSurface surface = RoadSurface::Collider;
};
struct VehicleState {
	std::array<WheelState, 4> wheels {};
	float steeringAngle = 0;
	float speed = 0;
	float driftEngagement = 0; // Signed by the direction of the initiating turn.
	float driftAngle = 0; // Signed body heading relative to planar travel, radians.
	float nitroCharge = 1;
	bool boosting = false;
};
struct DriftTuning {
	bool driftEnabled = true;
	float antiSkidStrength = 1.f;
	float breakawaySensitivity = .5f;
	float powerAngleDegrees = 26.929f;
	float recoverySeconds = 1.f / 1.8f;
	static constexpr float minAngle = 12.f;
	static constexpr float maxAngle = 45.f;
	static constexpr float minRecovery = .2f;
	static constexpr float maxRecovery = 1.5f;
	void validate() const;
};
struct Vehicle {
	DriftTuning driftTuning {};
	VehicleConfig config {};
	VehicleConfig authored {};
	COLLISION::Shape3D originalShape {};
	MATH::Matrix3f originalInverseInertia {};
	float originalMass = 1;
	VehicleProfile profile = VehicleProfile::Simulation;
	float assistance = 0;
	static constexpr float minHighSpeedSteering = .5f;
	static constexpr float maxHighSpeedSteering = 2.f;
	float highSpeedSteering = 1.f;
	SteeringMode steeringMode = SteeringMode::AutoCenter;
	TransmissionMode transmission = TransmissionMode::Direct;
	int gear = 1;
	float handbrake = 0;
	bool nitroRequested = false;
	float throttle = 0;
	float brake = 0;
	float steering = 0;
	VehicleState state {};
};
void stepVehicle(const PhysicWorld& world, const Body& chassis, Vehicle& vehicle, float dt, MATH::Vector3f& force, MATH::Vector3f& torque);
}
