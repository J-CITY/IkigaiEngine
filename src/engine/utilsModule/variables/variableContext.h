#pragma once
#include "utilsModule/expressions/expressionDescriptor.h"
#include "utilsModule/serialization/typedDescriptor.h"
#include <optional>

namespace IKIGAI::DATA {
using VariableId = size_t;
enum class VariableAccess { Local, Input, Computed };
struct VariableKeyDescriptor {
	std::string Name {};
	ValueType Type = ValueType::Bool;
	VariableAccess Access = VariableAccess::Local;
	std::optional<LiteralDescriptor> Default {};
	std::optional<ExpressionDescriptor> Expression {};
	bool Event = false;
	template <class C> static auto serde(C& c, VariableKeyDescriptor& v) {
		using namespace serde::attribute;
		serde::serde_struct(c, v)
			.field(&VariableKeyDescriptor::Name, "Name")
			.field(&VariableKeyDescriptor::Type, "Type")
			.field(&VariableKeyDescriptor::Access, "Access", default_ { VariableAccess::Local })
			.field(&VariableKeyDescriptor::Default, "Default")
			.field(&VariableKeyDescriptor::Expression, "Expression")
			.field(&VariableKeyDescriptor::Event, "Event", default_ { false });
	}
};
struct VariableSchemaDescriptor {
	int Version = 1;
	std::vector<VariableKeyDescriptor> Keys {};
	template <class C> static auto serde(C& c, VariableSchemaDescriptor& v) {
		serde::serde_struct(c, v).field(&VariableSchemaDescriptor::Version, "Version").field(&VariableSchemaDescriptor::Keys, "Keys");
	}
};
struct VariableSchema {
	std::vector<VariableKeyDescriptor> keys {};
	std::vector<std::optional<Value>> defaults {};
	std::vector<std::optional<Expression>> expressions {};
	std::map<std::string, VariableId> names {};
	Symbols symbols {};
	VariableId key(const std::string&) const;
	static std::shared_ptr<const VariableSchema> compile(const nlohmann::json&, const std::string& path = {});
	static std::shared_ptr<const VariableSchema> compile(const VariableSchemaDescriptor&);
};
class VariableContext {
public:
	using Evaluator = std::function<std::optional<Value>(const VariableContext&)>;
	explicit VariableContext(std::shared_ptr<const VariableSchema>);
	const VariableSchema& schema() const;
	const std::optional<Value>& readCurrent(VariableId) const;
	const std::optional<Value>& readSnapshot(VariableId) const;
	uint64_t version(VariableId) const;
	uint64_t tick() const;
	void write(VariableId, Value);
	void clear(VariableId);
	void fire(VariableId);
	void setInput(VariableId, std::optional<Value>);
	void bindComputation(VariableId, std::vector<VariableId>, Evaluator, bool poll = false);
	void publish();
	void expireEntities();
	void migrate(const VariableContext&);

private:
	struct Computation {
		std::vector<VariableId> dependencies {};
		Evaluator evaluate {};
		bool poll = false;
	};
	void assign(VariableId, std::optional<Value>) const;
	void evaluate(VariableId) const;
	void invalidateDependents(VariableId) const;
	void validateComputations() const;
	std::shared_ptr<const VariableSchema> mSchema {};
	mutable std::vector<std::optional<Value>> mCurrent {};
	std::vector<std::optional<Value>> mSnapshot {};
	mutable std::vector<uint64_t> mVersions {};
	std::vector<Computation> mComputations {};
	std::vector<std::vector<VariableId>> mDependents {};
	mutable std::vector<bool> mDirty {};
	mutable std::vector<bool> mEvaluating {};
	uint64_t mTick = 0;
};
}
