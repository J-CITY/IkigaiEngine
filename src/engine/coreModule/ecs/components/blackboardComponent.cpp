#include "blackboardComponent.h"
#include "coreModule/ecs/componentReference.inl"
#include "variableContextComponent.h"
namespace IKIGAI::ECS {
BlackboardComponent::BlackboardComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "BlackboardComponent";
	settings.Type = GetType<BlackboardComponent>();
}
BlackboardComponent::BlackboardComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: BlackboardComponent(object) {
	settings = static_cast<const Descriptor&>(descriptor);
}
BlackboardComponent::Descriptor BlackboardComponent::getDescriptor() const {
	return settings;
}
bool BlackboardComponent::Prepared::current() const {
	return owner && static_cast<const VariableContextComponent*>(owner.get())->runtime() == board->context();
}
std::shared_ptr<BlackboardComponent::Prepared> BlackboardComponent::prepare(
	SCENE_SYSTEM::Scene& scene, std::shared_ptr<const AI::BlackboardSchema> schema) const {
	auto context
		= settings.Context.empty() ? obj.get().getComponent<VariableContextComponent>() : Resolve<VariableContextComponent>(scene, settings.Context);
	if (!context || !context->runtime()) {
		throw std::runtime_error("Blackboard requires a prepared VariableContextComponent");
	}
	for (const auto& [name, variable] : settings.Bindings) {
		schema->key(name);
	}
	std::vector<size_t> slots;
	for (const auto& key : schema->keys) {
		const auto found = settings.Bindings.find(key.Name);
		slots.push_back(context->slot(found == settings.Bindings.end() ? key.Name : found->second));
	}
	auto candidate = std::make_shared<Prepared>();
	candidate->owner = UTILS::WeakPtr<ComponentBase>(static_cast<ComponentBase*>(context.get()));
	candidate->board = std::make_shared<AI::Blackboard>(std::move(schema), context->runtime(), std::move(slots));
	return candidate;
}
void BlackboardComponent::synchronize(SCENE_SYSTEM::Scene&, Prepared& prepared) {
	if (!prepared.current()) {
		throw std::runtime_error("Blackboard variable context was removed or replaced");
	}
	prepared.board->expireEntities();
}
void BlackboardComponent::install(std::shared_ptr<Prepared> prepared) {
	mPrepared = std::move(prepared);
}
std::shared_ptr<AI::Blackboard> BlackboardComponent::blackboard() const {
	return mPrepared ? mPrepared->board : nullptr;
}
void BlackboardComponent::onDestroy() {
	mPrepared.reset();
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::BlackboardComponent>();
	return true;
}();
}
