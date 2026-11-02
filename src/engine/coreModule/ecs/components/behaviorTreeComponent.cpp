#include "behaviorTreeComponent.h"
#include "aiModule/adapters/characterBehaviorHost.h"
#include "blackboardComponent.h"
#include "coreModule/ecs/componentReference.inl"
#include "resourceModule/behaviorTreeManager.h"
#include <iostream>

namespace IKIGAI::ECS {
struct BehaviorTreeComponent::State {
	std::shared_ptr<RESOURCES::BehaviorTreeResource> resource;
	std::shared_ptr<BlackboardComponent::Prepared> board;
	UTILS::WeakPtr<BlackboardComponent> target;
	std::string path;
	uint64_t revision = 0;
	uint64_t observedRevision = 0;
	std::unique_ptr<AI::CharacterBehaviorHost> host;
	// Destroy instance before its host and Blackboard storage.
	std::unique_ptr<AI::BehaviorTreeInstance> instance;
};
BehaviorTreeComponent::BehaviorTreeComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "BehaviorTreeComponent";
	settings.Type = GetType<BehaviorTreeComponent>();
}
BehaviorTreeComponent::BehaviorTreeComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: BehaviorTreeComponent(object) {
	settings = static_cast<const Descriptor&>(descriptor);
}
BehaviorTreeComponent::~BehaviorTreeComponent() {
	onDisable();
}
BehaviorTreeComponent::Descriptor BehaviorTreeComponent::getDescriptor() const {
	return settings;
}
void BehaviorTreeComponent::applyCommands(SCENE_SYSTEM::Scene&) {
	if (!mState) {
		return;
	}
	if (!mState->target || !mState->board->current() || settings.Tree.empty() || !obj->getIsActive() || !obj->isAlive() || mPaused || mStopped
		|| !mState->instance->running()) {
		mState->host->clear();
		return;
	}
	mState->host->apply();
}
void BehaviorTreeComponent::update(SCENE_SYSTEM::Scene& scene, double dt) {
	if (settings.Tree.empty()) {
		if (mState) {
			stop();
		}
		return;
	}
	if (mStopped) {
		return;
	}
	try {
		auto boardComponent = obj->getComponent<BlackboardComponent>();
		if (!boardComponent) {
			throw std::runtime_error("Behavior Tree requires an owning BlackboardComponent");
		}
		auto resource = mState && mState->path == settings.Tree
			? mState->resource
			: RESOURCES::ServiceManager::Get<RESOURCES::BehaviorTreeLoader>().loadResource(settings.Tree);
		if (!resource || !resource->definition) {
			throw std::runtime_error("Missing compiled Behavior Tree");
		}
		if (!mState || !mState->board->current() || mState->path != settings.Tree || mState->observedRevision != resource->revision || !mState->target
			|| mState->target.get() != boardComponent.get()) {
			try {
				if (RESOURCES::BehaviorTreeLoader::NormalizePath(boardComponent->settings.Schema)
					!= RESOURCES::BehaviorTreeLoader::NormalizePath(resource->definition->blackboardPath)) {
					throw std::runtime_error("Behavior Tree and BlackboardComponent use different schemas");
				}
				auto candidate = std::make_shared<State>();
				candidate->resource = resource;
				candidate->path = settings.Tree;
				candidate->revision = resource->revision;
				candidate->observedRevision = resource->revision;
				candidate->target = boardComponent;
				candidate->board = boardComponent->prepare(scene, resource->definition->schema);
				candidate->host = std::make_unique<AI::CharacterBehaviorHost>(obj->shared_from_this());
				candidate->host->bind(scene, settings.Motor);
				candidate->instance
					= std::make_unique<AI::BehaviorTreeInstance>(resource->definition, candidate->board->board, candidate->host.get());
				candidate->instance->pause(mPaused);
				if (mState) {
					mState->instance->stop();
				}
				boardComponent->install(candidate->board);
				mState = std::move(candidate);
				mError.clear();
			} catch (const std::exception& error) {
				if (!mState || !mState->target || !mState->board->current()) {
					throw;
				}
				// Rejected scene bindings keep the previous instance and its schema.
				mError = error.what();
				mState->observedRevision = resource->revision;
				std::cerr << "Behavior Tree instance reload rejected: " << mError << '\n';
			}
		}
		BlackboardComponent::synchronize(scene, *mState->board);
		mState->instance->update(dt);
	} catch (const std::exception& error) {
		mError = error.what();
		stop();
		std::cerr << "Behavior Tree stopped (" << settings.Tree << "): " << mError << '\n';
	}
}
void BehaviorTreeComponent::pause(bool value) {
	mPaused = value;
	if (mState) {
		mState->instance->pause(value);
	}
}
void BehaviorTreeComponent::stop() {
	mStopped = true;
	if (mState) {
		mState->instance->stop();
		mState->host->clear();
	}
}
void BehaviorTreeComponent::restart() {
	onDisable();
	mStopped = false;
}
AI::BehaviorTreeInstance* BehaviorTreeComponent::instance() const {
	return mState ? mState->instance.get() : nullptr;
}
nlohmann::json BehaviorTreeComponent::diagnostics() const {
	auto snapshot = mState ? mState->instance->snapshot() : nlohmann::json::object();
	snapshot["Revision"] = mState ? mState->revision : 0;
	snapshot["Error"] = mError;
	return snapshot;
}
void BehaviorTreeComponent::onDisable() {
	if (mState) {
		mState->instance->stop();
		mState->host->clear();
		mState.reset();
	}
	mStopped = false;
}
void BehaviorTreeComponent::onDestroy() {
	onDisable();
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::BehaviorTreeComponent>();
	return true;
}();
}
