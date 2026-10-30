#include "aiModule/tasks/taskParameters.h"
#include <cmath>

namespace IKIGAI::AI {
namespace {
	enum class KeyTest { IsSet, IsUnset, IsTrue, IsFalse, IsAlive };
	struct ConditionParameters {
		std::string Key;
		KeyTest Test = KeyTest::IsSet;
		template <class C> static auto serde(C& c, ConditionParameters& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v).field(&ConditionParameters::Key, "Key").field(&ConditionParameters::Test, "Test", default_ { KeyTest::IsSet });
		}
	};
	struct ExpressionParameters {
		DATA::ExpressionDescriptor Expression;
		template <class C> static auto serde(C& c, ExpressionParameters& v) {
			serde::serde_struct(c, v).field(&ExpressionParameters::Expression, "Expression");
		}
	};
	class KeyCondition final : public BehaviorDecorator {
	public:
		KeyCondition(BlackboardKeyId key, KeyTest test)
			: mKey(key)
			, mTest(test) { }
		std::vector<BlackboardKeyId> dependencies() const override {
			return { mKey };
		}
		bool test(const Blackboard& blackboard, double) const override {
			const auto& value = blackboard.read(mKey);
			switch (mTest) {
			case KeyTest::IsSet:
				return value.has_value();
			case KeyTest::IsUnset:
				return !value;
			case KeyTest::IsTrue:
				return value && std::get<bool>(*value);
			case KeyTest::IsFalse:
				return value && !std::get<bool>(*value);
			case KeyTest::IsAlive:
				return value && std::get<EntityRef>(*value).alive();
			}
			return false;
		}

	private:
		BlackboardKeyId mKey = 0;
		KeyTest mTest = KeyTest::IsSet;
	};
	class ExpressionCondition final : public BehaviorDecorator {
	public:
		explicit ExpressionCondition(DATA::Expression expression)
			: mExpression(std::move(expression)) { }
		std::vector<BlackboardKeyId> dependencies() const override {
			return mExpression.dependencies();
		}
		bool test(const Blackboard& blackboard, double) const override {
			for (const auto key : mExpression.dependencies()) {
				if (!blackboard.read(key)) {
					return false;
				}
			}
			return DATA::boolean(mExpression.evaluate([&](size_t key) { return blackboard.expressionValue(key); }));
		}

	private:
		DATA::Expression mExpression;
	};
	class TimedCondition final : public BehaviorDecorator {
	public:
		TimedCondition(double duration, bool limit)
			: mDuration(duration)
			, mLimit(limit) { }
		bool test(const Blackboard&, double time) const override {
			return mLimit ? !mActive || time < mDeadline : time >= mDeadline;
		}
		double wakeTime() const override {
			return mDeadline;
		}
		void enter(double time) override {
			mActive = true;
			if (mLimit) {
				mDeadline = time + mDuration;
			}
		}
		void exit(double time) override {
			mActive = false;
			mDeadline = mLimit ? std::numeric_limits<double>::infinity() : time + mDuration;
		}

	private:
		double mDuration = 0;
		double mDeadline = 0;
		bool mLimit = false;
		bool mActive = false;
	};
}
void registerBuiltinDecorators(BehaviorRegistry& registry) {
	registry.addDecorator("BlackboardCondition", [](const auto& json, const BlackboardSchema& schema) -> DecoratorFactory {
		const auto parameters = readDescriptor<ConditionParameters>(json);
		const auto key = schema.key(parameters.Key);
		const auto type = schema.keys[key].Type;
		if (((parameters.Test == KeyTest::IsTrue || parameters.Test == KeyTest::IsFalse) && type != BlackboardType::Bool)
			|| (parameters.Test == KeyTest::IsAlive && type != BlackboardType::EntityRef)) {
			throw std::runtime_error("BlackboardCondition type mismatch: " + parameters.Key);
		}
		return [key, parameters] { return std::make_unique<KeyCondition>(key, parameters.Test); };
	});
	registry.addDecorator("ExpressionCondition", [](const auto& json, const BlackboardSchema& schema) -> DecoratorFactory {
		const auto parameters = readDescriptor<ExpressionParameters>(json);
		const auto expression = DATA::Expression::compile(parameters.Expression, schema.symbols);
		if (expression.type() != DATA::ValueType::Bool) {
			throw std::runtime_error("ExpressionCondition must return Bool");
		}
		return [expression] { return std::make_unique<ExpressionCondition>(expression); };
	});
	for (const auto& [name, limit] : { std::pair { "Cooldown", false }, { "TimeLimit", true } }) {
		registry.addDecorator(name, [limit](const auto& json, const auto&) -> DecoratorFactory {
			const auto parameters = readDescriptor<DurationParameters>(json);
			if (!std::isfinite(parameters.Duration) || parameters.Duration <= 0) {
				throw std::runtime_error("Invalid decorator Duration");
			}
			return [parameters, limit] { return std::make_unique<TimedCondition>(parameters.Duration, limit); };
		});
	}
}
}
