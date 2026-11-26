#include "vehicle.h"
#include "worldQueries.h"
#include <algorithm>
#include <numbers>
#include <stdexcept>

namespace IKIGAI::PHYSICS {
ChassisDimensions chassisDimensions(ChassisStyle style) {
	switch (style) {
	case ChassisStyle::Sport:
		return { 1280, { .95f, .19f, 2.05f } };
	case ChassisStyle::Crossover:
		return { 1900, { .99f, .32f, 2.23f } };
	case ChassisStyle::Truck:
		return { 4100, { 1.18f, .44f, 2.95f } };
	default:
		return {};
	}
}
VehicleConfig vehiclePreset(ChassisStyle style) {
	VehicleConfig config;
	config.chassis = style;
	switch (style) {
	case ChassisStyle::Sport:
		config.suspension = SuspensionStyle::Sport;
		config.tires = TireStyle::Performance;
		config.engine = EngineStyle::Sport;
		config.brakes = BrakeStyle::Sport;
		config.centerOfMass.y = -.12f;
		break;
	case ChassisStyle::Crossover:
		config.suspension = SuspensionStyle::Touring;
		config.tires = TireStyle::AllTerrain;
		config.drive = DriveLayout::All;
		config.engine = EngineStyle::Utility;
		config.centerOfMass.y = .08f;
		break;
	case ChassisStyle::Truck:
		config.suspension = SuspensionStyle::HeavyDuty;
		config.tires = TireStyle::AllTerrain;
		config.engine = EngineStyle::Diesel;
		config.brakes = BrakeStyle::HeavyDuty;
		config.centerOfMass = { 0, .19f, .16f };
		break;
	default:
		break;
	}
	return config;
}
VehicleConfig VehicleConfig::resolved() const {
	auto result = *this;
	if (chassis != ChassisStyle::Authored) {
		const auto h = chassisDimensions(chassis).halfSize;
		result.trackWidth = h.x * 1.86f;
		result.wheelBase = h.z * 1.54f;
		constexpr std::array radii { .32f, .39f, .52f };
		result.radius = radii.at(static_cast<size_t>(chassis) - 1);
	}
	if (suspension != SuspensionStyle::Authored) {
		struct SpringKit {
			float length;
			float travel;
			float rate;
			float damping;
			float antiRoll;
			float loadLimit;
		};
		constexpr std::array kits {
			SpringKit { .28f, .12f, 55000, 4200, 22000, 21000 }, // Sport
			SpringKit { .44f, .22f, 37000, 3500, 12000, 21000 }, // Touring
			SpringKit { .55f, .29f, 90000, 8000, 30000, 48000 } // Heavy duty
		};
		const auto& kit = kits.at(static_cast<size_t>(suspension) - 1);
		result.restLength = kit.length;
		result.maxCompression = kit.travel;
		result.maxDroop = kit.travel;
		result.stiffness = kit.rate;
		result.compressionDamping = kit.damping;
		result.reboundDamping = kit.damping * 1.45f;
		result.antiRollStiffness = kit.antiRoll;
		result.maxSuspensionForce = kit.loadLimit;
	}
	switch (engine) {
	case EngineStyle::Sport:
		result.driveTorque = 2200;
		break;
	case EngineStyle::Utility:
		result.driveTorque = 1650;
		break;
	case EngineStyle::Diesel:
		result.driveTorque = 4100;
		break;
	default:
		break;
	}
	return result;
}
RoadResponse tireResponse(TireStyle tire, RoadSurface surface, float colliderFriction) {
	// Grip for road, performance, all-terrain and winter tires. Last column is Crr.
	static constexpr float responses[][5] = {
		{ 1, 1.08f, .94f, .92f, 0 }, // Existing collider friction
		{ 1, 1.18f, .93f, .91f, .012f }, // Dry asphalt
		{ .10f, .07f, .12f, .21f, .014f }, // Ice
		{ .35f, .21f, .62f, .40f, .11f }, // Mud
		{ .48f, .31f, .65f, .49f, .095f }, // Sand
		{ .30f, .17f, .42f, .64f, .052f }, // Snow
		{ .70f, .73f, .67f, .71f, .018f }, // Wet asphalt
		{ .66f, .49f, .83f, .65f, .038f } // Dirt road
	};
	if (tire < TireStyle::Road || tire > TireStyle::Winter || surface < RoadSurface::Collider || surface > RoadSurface::DirtRoad) {
		throw std::invalid_argument("Unknown tire or surface");
	}
	const auto& row = responses[static_cast<size_t>(surface)];
	return { row[static_cast<size_t>(tire)] * (surface == RoadSurface::Collider ? colliderFriction : 1.f), row[4] };
}
void VehicleConfig::validate() const {
	if (chassis < ChassisStyle::Authored || chassis > ChassisStyle::Truck || suspension < SuspensionStyle::Authored
		|| suspension > SuspensionStyle::HeavyDuty || tires < TireStyle::Road || tires > TireStyle::Winter || brakes < BrakeStyle::Standard
		|| brakes > BrakeStyle::HeavyDuty || engine < EngineStyle::Authored || engine > EngineStyle::Diesel || drive < DriveLayout::Rear
		|| drive > DriveLayout::All) {
		throw std::invalid_argument("Unknown vehicle part");
	}
	for (float value : { massOverrideKg, wheelBase, trackWidth, radius, restLength, maxCompression, maxDroop, stiffness, compressionDamping,
		 reboundDamping, antiRollStiffness, maxSuspensionForce, driveTorque }) {
		if (!std::isfinite(value) || value < 0) {
			throw std::invalid_argument("Invalid vehicle configuration");
		}
	}
	if (!std::isfinite(centerOfMass.x) || !std::isfinite(centerOfMass.y) || !std::isfinite(centerOfMass.z)) {
		throw std::invalid_argument("Invalid centre of mass");
	}
	if (wheelBase <= 0 || trackWidth <= 0 || radius <= 0 || restLength <= 0 || maxCompression > restLength || stiffness <= 0
		|| maxSuspensionForce <= 0) {
		throw std::invalid_argument("Invalid wheel geometry or suspension");
	}
}
Vec3 VehicleConfig::mount(std::size_t wheel) const {
	return { (wheel % 2 ? -.5f : .5f) * trackWidth, 0, (wheel < 2 ? .5f : -.5f) * wheelBase };
}

void DriftTuning::validate() const {
	if (!(antiSkidStrength >= 0 && antiSkidStrength <= 1 && breakawaySensitivity >= 0 && breakawaySensitivity <= 1 && powerAngleDegrees >= minAngle
			&& powerAngleDegrees <= maxAngle && recoverySeconds >= minRecovery && recoverySeconds <= maxRecovery)) {
		throw std::invalid_argument("Invalid arcade drift tuning");
	}
}

namespace {
	void updateDriftEngagement(Vehicle& vehicle, float longitudinalSpeed, float steerFraction, bool forwardGear, float assist, float dt) {
		const bool axleContact = (vehicle.state.wheels[0].normalLoad > 0 || vehicle.state.wheels[1].normalLoad > 0)
			&& (vehicle.state.wheels[2].normalLoad > 0 || vehicle.state.wheels[3].normalLoad > 0);
		auto& engagement = vehicle.state.driftEngagement;
		const auto& tuning = vehicle.driftTuning;
		const float entrySteer = std::lerp(.32f, .04f, tuning.breakawaySensitivity);
		float targetEngagement = 0;
		if (assist > 0 && axleContact && forwardGear && longitudinalSpeed > 5.f && vehicle.brake < .1f && vehicle.throttle >= 0) {
			if (vehicle.handbrake > .35f && longitudinalSpeed > 10.f && std::abs(steerFraction) > entrySteer) {
				targetEngagement = std::copysign(1.f, std::abs(engagement) > .05f ? engagement : steerFraction);
			}
			else if (std::abs(engagement) > .05f && vehicle.throttle > .05f && vehicle.handbrake < .35f) {
				targetEngagement = std::copysign(1.f, engagement);
			}
		}
		const bool straightened = vehicle.handbrake < .1f && steerFraction * engagement < -.3f && std::abs(vehicle.state.driftAngle) < .09f;
		if (!tuning.driftEnabled || straightened || !axleContact || !forwardGear || assist == 0 || longitudinalSpeed <= 0) {
			engagement = 0;
		}
		else {
			const float transition = (targetEngagement == 0 ? 1.f / tuning.recoverySeconds : std::lerp(2.f, 10.f, tuning.breakawaySensitivity)) * dt;
			engagement += std::clamp(targetEngagement - engagement, -transition, transition);
		}
	}
}
void stepVehicle(const PhysicWorld& world, const Body& chassis, Vehicle& vehicle, float dt, Vec3& force, Vec3& torque) {
	const auto& config = vehicle.config;
	const float assist = vehicle.profile == VehicleProfile::Casual ? 1.f : std::clamp(vehicle.assistance, 0.f, 1.f);
	const Vec3 up = chassis.getDirectionInWorldSpace({ 0, 1, 0 });
	const float speed = std::sqrt(chassis.getVelocity().squareMagnitude());
	vehicle.state.speed = speed;
	const float speedBlend = std::clamp((speed - 10.f) / 20.f, 0.f, 1.f);
	const float steeringScale = std::lerp(1.f, vehicle.highSpeedSteering, speedBlend);
	const float steeringLimit = steeringScale * std::lerp(.60f / (1.f + .015f * speed), .74f / (1.f + .047f * speed), assist);
	const float target = std::clamp(vehicle.steering, -1.f, 1.f) * steeringLimit;
	auto& angle = vehicle.state.steeringAngle;
	if (vehicle.steeringMode == SteeringMode::HoldAngle) {
		angle = std::clamp(angle + 2.1f * dt * std::clamp(vehicle.steering, -1.f, 1.f) * steeringLimit / .60f, -.60f, .60f);
	}
	else {
		const bool returning = vehicle.steering == 0 || angle * target < 0;
		const float slew = std::lerp(2.1f, returning ? 13.f : 9.f, assist);
		angle += std::clamp(target - angle, -slew * dt, slew * dt);
	}
	const float travel = config.restLength + config.maxDroop + config.radius;
	QUERY::Filter filter;
	filter.ignore = chassis.id;
	filter.mask = chassis.mask;
	filter.layer = chassis.layer;
	constexpr float wheelInertia = 1.2f;
	std::array<QUERY::Hit, 4> contacts {};
	for (size_t i = 0; i < contacts.size(); ++i) {
		auto& wheel = vehicle.state.wheels[i];
		const Vec3 mount = chassis.getPointInWorldSpace(config.mount(i) - chassis.centerOfMass);
		auto& hit = contacts[i];
		hit = QUERY::raycast(world, mount, -up * travel, filter);
		wheel.length = config.restLength + config.maxDroop;
		wheel.normalLoad = 0;
		wheel.slipSpeed = 0;
		wheel.surface = hit ? world.bodies().at(hit.body).roadSurface : RoadSurface::Collider;
		const float alignment = up.dot(hit.normal);
		if (!hit || alignment <= .2f) {
			continue;
		}
		const float length = hit.distance - config.radius;
		const float compression = config.restLength - length;
		wheel.length = std::max(length, config.restLength - config.maxCompression);
		const Vec3 pointVelocity = chassis.getVelocity() + chassis.getRotation().cross(hit.point - chassis.getPosition());
		const float extensionSpeed = pointVelocity.dot(hit.normal) / alignment;
		const float damping = extensionSpeed < 0 ? config.compressionDamping : config.reboundDamping;
		const float bumpStop = std::max(0.f, compression - config.maxCompression) * 3 * config.stiffness;
		wheel.normalLoad
			= std::clamp((compression * config.stiffness + bumpStop - damping * extensionSpeed) / alignment, 0.f, config.maxSuspensionForce);
	}
	for (size_t axle = 0; axle < 4; axle += 2) {
		auto& left = vehicle.state.wheels[axle];
		auto& right = vehicle.state.wheels[axle + 1];
		if (left.normalLoad > 0 && right.normalLoad > 0) {
			const float transfer = std::clamp((right.length - left.length) * config.antiRollStiffness,
				-std::min(left.normalLoad, config.maxSuspensionForce - right.normalLoad),
				std::min(right.normalLoad, config.maxSuspensionForce - left.normalLoad));
			left.normalLoad += transfer;
			right.normalLoad -= transfer;
		}
	}
	const float drivenWheelCount = config.drive == DriveLayout::All ? 4.f : 2.f;
	float driveInput = std::clamp(vehicle.throttle, -1.f, 1.f);
	if (vehicle.transmission != TransmissionMode::Direct) {
		constexpr std::array ratios { -.90f, 0.f, 1.f, .72f, .52f, .40f, .32f };
		const int gear = std::clamp(vehicle.gear, -1, 5);
		const float ratio = vehicle.transmission == TransmissionMode::Manual ? ratios[gear + 1] : (gear < 0 ? -1.f : 1.f);
		driveInput = std::max(driveInput, 0.f) * ratio;
	}
	const bool forwardGear = vehicle.transmission == TransmissionMode::Direct || vehicle.gear > 0;
	const bool grounded = std::ranges::any_of(vehicle.state.wheels, [](const auto& wheel) { return wheel.normalLoad > 0; });
	auto& state = vehicle.state;
	state.boosting = vehicle.nitroRequested && forwardGear && driveInput > 0 && vehicle.brake == 0 && vehicle.handbrake == 0 && grounded
		&& state.nitroCharge > 0;
	const float boostFraction = state.boosting ? std::min(1.f, state.nitroCharge * 4.f / dt) : 0.f;
	if (state.boosting) {
		state.nitroCharge = std::max(0.f, state.nitroCharge - dt / 4.f);
	}
	else if (!vehicle.nitroRequested) {
		state.nitroCharge = std::min(1.f, state.nitroCharge + dt / 10.f);
	}
	// Arcade gearing delivers strong launch torque and progressively trades it for speed.
	// All acceleration still passes through the selected driven wheels and the contact patch.
	const float longitudinalSpeed = chassis.getVelocity().dot(chassis.getDirectionInWorldSpace({ 0, 0, 1 }));
	const Vec3 sideways = chassis.getDirectionInWorldSpace({ 1, 0, 0 });
	const Vec3 planarVelocity = chassis.getVelocity() - up * chassis.getVelocity().dot(up);
	const float planarSpeed = planarVelocity.magnitude();
	const Vec3 travelDirection = planarSpeed > .1f ? planarVelocity * (1.f / planarSpeed) : Vec3 {};
	const float slip = std::atan2(-planarVelocity.dot(sideways), longitudinalSpeed);
	state.driftAngle = planarSpeed > 1.f ? slip : 0.f;
	const float steerFraction = std::clamp(angle / steeringLimit, -1.f, 1.f);
	updateDriftEngagement(vehicle, longitudinalSpeed, steerFraction, forwardGear, assist, dt);
	const float drift = std::abs(state.driftEngagement) * assist;
	Vec3 contactForce {};
	float tireYawTorque = 0;
	float availableGrip = 0;
	const bool reverse = driveInput < 0.f;
	const float progress = std::clamp((reverse ? -longitudinalSpeed : longitudinalSpeed) / (reverse ? 15.f : 72.f + 14.f * boostFraction), 0.f, 1.f);
	const float arcadeDrive = (reverse ? 2.8f : 6.4f) * (1.f - progress * progress);
	const float drivePerWheel
		= (1.f - .55f * drift) * config.driveTorque * driveInput * std::lerp(1.f, arcadeDrive, assist) * (1.f + boostFraction) / drivenWheelCount;
	// Share the extra grip between steering and acceleration. The handbrake can still lock the rear wheels.
	const float gripMultiplier = 1.f + assist * (1.8f + .65f * boostFraction);
	constexpr std::array brakeTorques { 1900.f, 3300.f, 5500.f }; // Standard, sport, heavy duty (Nm per front wheel)
	const float maxFrontBrakeTorque = brakeTorques.at(static_cast<size_t>(config.brakes)) * std::clamp(vehicle.brake, 0.f, 1.f);
	for (size_t i = 0; i < contacts.size(); ++i) {
		auto& wheel = vehicle.state.wheels[i];
		const auto& hit = contacts[i];
		const auto localMount = config.mount(i);
		wheel.steer = i < 2 ? std::atan2(config.wheelBase * std::sin(angle), config.wheelBase * std::cos(angle) - localMount.x * std::sin(angle)) : 0;
		const bool front = i < 2;
		const bool driven
			= config.drive == DriveLayout::All || (front && config.drive == DriveLayout::Front) || (!front && config.drive == DriveLayout::Rear);
		float drive = driven ? drivePerWheel : 0;
		if (vehicle.transmission == TransmissionMode::Manual && vehicle.gear != 0) {
			constexpr std::array redlineSpeed { 13.f, 0.f, 17.f, 24.f, 33.f, 43.f, 54.f }; // tyre speed in m/s
			const float limit = redlineSpeed[std::clamp(vehicle.gear, -1, 5) + 1];
			drive *= std::clamp((limit - std::abs(wheel.angularSpeed) * config.radius) / (.12f * limit), 0.f, 1.f);
		}
		const float brakeLimit = maxFrontBrakeTorque * (front ? 1.f : .65f)
			+ (front ? 0.f : brakeTorques.at(static_cast<size_t>(config.brakes)) * std::clamp(vehicle.handbrake, 0.f, 1.f));
		if (assist > 0 && driven && wheel.normalLoad > 0) {
			// Available contact friction bounds the assisted drive torque, including on ice.
			const auto road = tireResponse(config.tires, wheel.surface, world.bodies().at(hit.body).friction);
			const float tractionTorque = road.grip * wheel.normalLoad * config.radius * gripMultiplier;
			drive = std::lerp(drive, std::clamp(drive, -tractionTorque, tractionTorque), assist);
		}
		wheel.angularSpeed += drive * dt / wheelInertia;
		if (wheel.normalLoad > 0) {
			const Vec3 arm = hit.point - chassis.getPosition();
			const Vec3 velocity = chassis.getVelocity() + chassis.getRotation().cross(arm);
			const Vec3 heading = chassis.getDirectionInWorldSpace({ std::sin(wheel.steer), 0, std::cos(wheel.steer) });
			const Vec3 side = MATH::Vector3f::Normalize(hit.normal.cross(heading));
			const Vec3 forward = side.cross(hit.normal);
			const float longitudinalSpeed = velocity.dot(forward);
			// Implicit wheel reaction keeps the stiff slip law stable through zero speed.
			const float stiffness = 80000.f / std::max(1.f, std::abs(longitudinalSpeed));
			const auto response = tireResponse(config.tires, wheel.surface, world.bodies().at(hit.body).friction);
			const float grip = response.grip * wheel.normalLoad * gripMultiplier;
			// Torque required to hold the wheel at zero against the road's reaction.
			const float lockedForce = std::clamp(-stiffness * longitudinalSpeed, -grip, grip);
			const float holdTorque = wheel.angularSpeed * wheelInertia / dt - lockedForce * config.radius;
			wheel.angularSpeed -= std::clamp(holdTorque, -brakeLimit, brakeLimit) * dt / wheelInertia;
			float longitudinal = stiffness * (wheel.angularSpeed * config.radius - longitudinalSpeed)
				/ (1 + stiffness * dt * config.radius * config.radius / wheelInertia);
			wheel.slipSpeed = std::hypot(wheel.angularSpeed * config.radius - longitudinalSpeed, velocity.dot(side));
			float lateral = -60000.f * std::atan2(velocity.dot(side), std::max(1.f, std::abs(longitudinalSpeed)));
			// Bound lateral impulse by the supported quarter mass near standstill.
			const float lateralLimit = std::abs(velocity.dot(side)) * chassis.getMass() / (4 * dt);
			lateral = std::clamp(lateral, -lateralLimit, lateralLimit);

			lateral *= 1.f - (front ? .25f : .65f) * drift;
			// Scale only lateral grip so fast saturated turns respond without changing straight-line traction.
			const float demand = std::hypot(longitudinal, lateral / steeringScale);
			if (demand > grip) {
				const float scale = grip / demand;
				longitudinal *= scale;
				lateral *= scale;
			}
			wheel.angularSpeed -= longitudinal * config.radius * dt / wheelInertia;
			const float rollingDrag = response.rollingResistance * wheel.normalLoad * std::tanh(longitudinalSpeed);
			const Vec3 support = hit.normal * wheel.normalLoad;
			// Redirect part of the lateral scrub across the path during an arcade slide.
			// A handbrake or service brake still removes speed through the longitudinal contact force.
			Vec3 cornerForce = side * lateral;
			cornerForce -= travelDirection * (std::min(0.f, cornerForce.dot(travelDirection)) * drift);
			const Vec3 tireForce = forward * (longitudinal - rollingDrag) + cornerForce;
			contactForce += tireForce;
			availableGrip += grip;
			// A shorter vertical lever arm limits arcade body roll and pitch without changing suspension geometry or yaw leverage.
			const Vec3 tireArm = arm - up * (arm.dot(up) * .78f * assist);
			force += support + tireForce;
			const Vec3 tireTorque = tireArm.cross(tireForce);
			tireYawTorque += tireTorque.dot(up);
			torque += arm.cross(support) + tireTorque;
		}
		else if (brakeLimit > 0) {
			const float delta = brakeLimit * dt / wheelInertia;
			wheel.angularSpeed -= std::clamp(wheel.angularSpeed, -delta, delta);
		}
		wheel.rotation = std::remainder(wheel.rotation + wheel.angularSpeed * dt, 2 * std::numbers::pi_v<float>);
	}
	// With drift disabled, catch excess yaw relative to the actual curved path.
	const bool stabilize = !vehicle.driftTuning.driftEnabled && vehicle.driftTuning.antiSkidStrength > 0.f && forwardGear && longitudinalSpeed > 2.f
		&& (state.wheels[0].normalLoad > 0 || state.wheels[1].normalLoad > 0) && (state.wheels[2].normalLoad > 0 || state.wheels[3].normalLoad > 0);
	if ((drift > 0 || stabilize) && planarSpeed > 1.f) {
		const float pathYawRate = travelDirection.cross(contactForce).dot(up) / (chassis.getMass() * planarSpeed);
		const float yawRate = chassis.getRotation().dot(up);
		float wantedYawRate = pathYawRate;
		float strength = drift;
		if (stabilize) {
			// Follow the steered arc within the contact grip left after acceleration or braking.
			strength = vehicle.driftTuning.antiSkidStrength * std::clamp((longitudinalSpeed - 2.f) / 4.f, 0.f, 1.f);
			const Vec3 turnSide = up.cross(travelDirection);
			const float alongForce = contactForce.dot(travelDirection);
			const float lateralBudget = steeringScale * std::sqrt(std::max(0.f, availableGrip * availableGrip - alongForce * alongForce));
			const float massSpeed = chassis.getMass() * planarSpeed;
			const float steeringYawRate = longitudinalSpeed * std::tan(angle) / config.wheelBase;
			const float desiredLateral = std::clamp(massSpeed * steeringYawRate, -lateralBudget, lateralBudget);
			const float lateralCorrection = (desiredLateral - contactForce.dot(turnSide)) * strength;
			force += turnSide * lateralCorrection;
			wantedYawRate += lateralCorrection / massSpeed;
			wantedYawRate -= 5.f * (slip - std::clamp(slip, -.025f, .025f));
		}
		else {
			// Track the changing travel heading plus a driver-controlled sideslip angle.
			// Countersteering reduces that angle; releasing the throttle blends normal tyre response back in.
			const float direction = std::copysign(1.f, state.driftEngagement);
			const float throttle = std::clamp(vehicle.throttle, 0.f, 1.f);
			const float powerAngle = vehicle.driftTuning.powerAngleDegrees * std::numbers::pi_v<float> / 180.f;
			const float baseAngle = std::lerp(.22f, powerAngle, throttle);
			const float steerIntoSlide = direction * steerFraction;
			// Scale countersteer authority with the power angle so even a wide slide can be caught under throttle.
			const float steerAuthority = steerIntoSlide < 0 ? std::lerp(.5f, powerAngle + .03f, throttle) : .24f;
			const float targetSlip = state.driftEngagement * (baseAngle + steerAuthority * steerIntoSlide);
			const bool recovering = throttle < .05f || steerIntoSlide < -.1f;
			const float recoveryResponse = recovering ? std::clamp(1.f / (1.8f * vehicle.driftTuning.recoverySeconds), .5f, 2.f) : 1.f;
			wantedYawRate += 4.f * recoveryResponse * (targetSlip - slip);
		}
		const float inverseYawInertia = up.dot(chassis.getInverseInertiaTensorWorld().transform(up));
		if (inverseYawInertia > 0) {
			const float wantedTorque = (wantedYawRate - yawRate) * 10.f / inverseYawInertia;
			const float limit = availableGrip * config.wheelBase * .5f;
			torque += up * (std::clamp(wantedTorque - tireYawTorque, -limit, limit) * strength);
		}
	}
}
}
