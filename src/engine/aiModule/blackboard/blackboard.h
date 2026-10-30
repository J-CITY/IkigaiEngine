#pragma once
#include "aiModule/descriptor.h"
#include "utilsModule/variables/variableContext.h"

namespace IKIGAI::AI {
using BlackboardKeyId = DATA::VariableId;
using BlackboardType = DATA::ValueType;
using KeyAccess = DATA::VariableAccess;
using EntityRef = DATA::EntityRef;
using BlackboardValue = DATA::Value;
using BlackboardKeyDescriptor = DATA::VariableKeyDescriptor;
using BlackboardDescriptor = DATA::VariableSchemaDescriptor;
using BlackboardSchema = DATA::VariableSchema;

class Blackboard {
public:
	explicit Blackboard(std::shared_ptr<const BlackboardSchema>);
	Blackboard(std::shared_ptr<const BlackboardSchema>, std::shared_ptr<DATA::VariableContext>, std::vector<BlackboardKeyId>);
	const BlackboardSchema& schema() const;
	const std::optional<BlackboardValue>& read(BlackboardKeyId) const;
	DATA::Value expressionValue(BlackboardKeyId) const;
	void write(BlackboardKeyId, BlackboardValue);
	void clear(BlackboardKeyId);
	void setInput(BlackboardKeyId, std::optional<BlackboardValue>);
	uint64_t version(BlackboardKeyId) const;
	std::vector<BlackboardKeyId> takeChanges();
	void expireEntities();
	void migrate(const Blackboard&);
	std::shared_ptr<DATA::VariableContext> context() const;

private:
	std::shared_ptr<const BlackboardSchema> mSchema {};
	std::shared_ptr<DATA::VariableContext> mContext {};
	std::vector<BlackboardKeyId> mSlots {};
	std::vector<uint64_t> mObservedVersions {};
};
BlackboardValue blackboardValue(const DATA::Value&);
}
