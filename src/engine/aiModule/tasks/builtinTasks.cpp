#include "taskParameters.h"
#include <cmath>

namespace IKIGAI::AI {
namespace {
	class WaitTask final : public BehaviorTask {
	public:
		explicit WaitTask(double duration)
			: mDuration(duration) { }
		TaskResult enter(TaskContext& context) override {
			mDeadline = context.time + mDuration;
			return mDuration == 0 ? TaskResult::Success : TaskResult::Running;
		}
		TaskResult tick(TaskContext&) override {
			return TaskResult::Success;
		}
		double wakeTime() const override {
			return mDeadline;
		}

	private:
		double mDuration = 0;
		double mDeadline = 0;
	};
	class ResultTask final : public BehaviorTask {
	public:
		explicit ResultTask(TaskResult result)
			: mResult(result) { }
		TaskResult enter(TaskContext&) override {
			return mResult;
		}

	private:
		TaskResult mResult = TaskResult::Failure;
	};
	class WriteTask final : public BehaviorTask {
	public:
		WriteTask(BlackboardKeyId key, std::optional<BlackboardValue> value)
			: mKey(key)
			, mValue(std::move(value)) { }
		TaskResult enter(TaskContext& context) override {
			if (mValue) {
				context.blackboard.write(mKey, *mValue);
			}
			else {
				context.blackboard.clear(mKey);
			}
			return TaskResult::Success;
		}

	private:
		BlackboardKeyId mKey = 0;
		std::optional<BlackboardValue> mValue;
	};
	class MoveTask final : public BehaviorTask {
	public:
		MoveTask(BlackboardKeyId target, MoveParameters parameters, bool faceOnly)
			: mTarget(target)
			, mParameters(std::move(parameters))
			, mFaceOnly(faceOnly) { }
		TaskResult enter(TaskContext& context) override {
			mDeadline = context.time + mParameters.Timeout;
			return tick(context);
		}
		bool ticks() const override {
			return true;
		}
		TaskResult tick(TaskContext& context) override {
			if (!context.host || context.time >= mDeadline) {
				return TaskResult::Failure;
			}
			const auto owner = context.host->ownerPosition();
			const auto& value = context.blackboard.read(mTarget);
			if (!owner || !value) {
				return TaskResult::Failure;
			}
			std::optional<MATH::Vector3f> destination;
			if (const auto* vector = std::get_if<MATH::Vector3f>(&*value)) {
				destination = *vector;
			}
			else if (const auto* entity = std::get_if<EntityRef>(&*value); entity && entity->alive()) {
				destination = context.host->position(*entity);
			}
			if (!destination) {
				return TaskResult::Failure;
			}
			const float x = destination->x - owner->x;
			const float z = destination->z - owner->z;
			const float distance = std::sqrt(x * x + z * z);
			if (distance <= mParameters.AcceptanceRadius) {
				return TaskResult::Success;
			}
			const float heading = std::atan2(x, z) * (180.f / MATH::PI);
			if (mFaceOnly) {
				const auto current = context.host->ownerHeading();
				if (!current) {
					return TaskResult::Failure;
				}
				if (std::abs(std::remainder(heading - *current, 360.f)) <= 2.f) {
					return TaskResult::Success;
				}
			}
			const auto velocity
				= mFaceOnly ? MATH::Vector3f {} : MATH::Vector3f { x / distance * mParameters.Speed, 0, z / distance * mParameters.Speed };
			return context.host->move(context.token, velocity, heading) ? TaskResult::Running : TaskResult::Failure;
		}

	private:
		BlackboardKeyId mTarget = 0;
		MoveParameters mParameters;
		double mDeadline = 0;
		bool mFaceOnly = false;
	};
	BlackboardKeyId writableKey(const BlackboardSchema& schema, const std::string& name) {
		const auto key = schema.key(name);
		if (schema.keys[key].Access != KeyAccess::Local) {
			throw std::runtime_error("Task writes Input key: " + name);
		}
		return key;
	}
}
void registerBuiltinTasks(BehaviorRegistry& registry) {
	registry.addTask("Wait", [](const auto& json, const auto&) -> TaskFactory {
		const auto parameters = readDescriptor<DurationParameters>(json);
		if (!std::isfinite(parameters.Duration) || parameters.Duration < 0) {
			throw std::runtime_error("Invalid Wait Duration");
		}
		return [parameters] { return std::make_unique<WaitTask>(parameters.Duration); };
	});
	for (const auto& [name, result] : { std::pair { "Succeed", TaskResult::Success }, { "Fail", TaskResult::Failure } }) {
		registry.addTask(name, [result](const auto& json, const auto&) -> TaskFactory {
			readDescriptor<EmptyParameters>(json);
			return [result] { return std::make_unique<ResultTask>(result); };
		});
	}
	registry.addTask("SetBlackboardValue", [](const auto& json, const BlackboardSchema& schema) -> TaskFactory {
		const auto parameters = readDescriptor<SetValueParameters>(json);
		const auto key = writableKey(schema, parameters.Key);
		const auto value = blackboardValue(parameters.Value.value());
		if (size_t(schema.keys[key].Type) != value.index()) {
			throw std::runtime_error("SetBlackboardValue type mismatch: " + parameters.Key);
		}
		return [key, value] { return std::make_unique<WriteTask>(key, value); };
	});
	registry.addTask("ClearBlackboardValue", [](const auto& json, const BlackboardSchema& schema) -> TaskFactory {
		const auto parameters = readDescriptor<KeyParameters>(json);
		const auto key = writableKey(schema, parameters.Key);
		return [key] { return std::make_unique<WriteTask>(key, std::nullopt); };
	});
	for (const auto& [name, faceOnly] : { std::pair { "MoveDirectlyTo", false }, { "FaceTarget", true } }) {
		registry.addTask(name, [faceOnly](const auto& json, const BlackboardSchema& schema) -> TaskFactory {
			const auto parameters = readDescriptor<MoveParameters>(json);
			const auto key = schema.key(parameters.Target);
			const auto type = schema.keys[key].Type;
			if ((type != BlackboardType::Vector3 && type != BlackboardType::EntityRef) || !std::isfinite(parameters.Speed) || parameters.Speed <= 0
				|| !std::isfinite(parameters.AcceptanceRadius) || parameters.AcceptanceRadius <= 0 || !std::isfinite(parameters.Timeout)
				|| parameters.Timeout <= 0) {
				throw std::runtime_error("Invalid movement target, Speed, AcceptanceRadius or Timeout");
			}
			return [key, parameters, faceOnly] { return std::make_unique<MoveTask>(key, parameters, faceOnly); };
		});
	}
}
}
