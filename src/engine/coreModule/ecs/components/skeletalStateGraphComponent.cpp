#include "skeletalStateGraphComponent.h"
#include "coreModule/ecs/componentReference.inl"
#include "coreModule/testHooks.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/skeletonStateGraphManager.h"
#include "skeletalAnimationComponent.h"
#include "skeletalComponent.h"
#include "variableContextComponent.h"
#if USE_PHYSICS_3D
#include "characterMotorComponent.h"
#endif
#include <iostream>

namespace IKIGAI::ECS {
SkeletalStateGraphComponent::SkeletalStateGraphComponent(UTILS::Ref<Object> o)
	: ComponentBase(o) {
	__NAME__ = "SkeletalStateGraphComponent";
	settings.Type = GetType<SkeletalStateGraphComponent>();
}
SkeletalStateGraphComponent::SkeletalStateGraphComponent(UTILS::Ref<Object> o, const ComponentBase::Descriptor& d)
	: SkeletalStateGraphComponent(o) {
	settings = static_cast<const Descriptor&>(d);
}
void SkeletalStateGraphComponent::onDisable() {
	if (playbackTarget) {
		static_cast<SkeletalAnimationComponent*>(playbackTarget.get())->releasePlayback(*this);
	}
	playbackTarget = nullptr;
	instance.reset();
	environment.reset();
	bindings.clear();
	boundContext = nullptr;
	lastTick = 0;
}
void SkeletalStateGraphComponent::onDestroy() {
	onDisable();
}
void SkeletalStateGraphComponent::update(SCENE_SYSTEM::Scene& scene, float dt) {
	if (settings.Graph.empty()) {
		return;
	}
	auto animator = Resolve<SkeletalAnimationComponent>(scene, settings.Animator);
	auto context = Resolve<VariableContextComponent>(scene, settings.Context);
	if (!animator || !context) {
		std::cerr << "State graph context/animator reference is missing on " << obj->getName() << std::endl;
		return;
	}
	auto resource = RESOURCES::ServiceManager::Get<RESOURCES::SkeletonStateGraphLoader>().loadResource(settings.Graph);
	if (!resource || !resource->definition) {
		std::cerr << "State graph component requires a compiled graph: " << settings.Graph << std::endl;
		return;
	}
	if (!instance) {
		auto skeleton = animator->obj->getComponent<SkeletalComponent>();
		if (!skeleton || !skeleton->getSkeleton()) {
			std::cerr << "State graph animator has no skeleton on " << obj->getName() << std::endl;
			return;
		}
		environment = std::make_shared<UTILS::Environment>();
		// The instance keeps the loader resource alive (the loader cache is weak).
		// The owner validates context bindings before publishing a new definition.
		instance = std::make_shared<SKELETON::SkeletalStateGraphInstance>(skeleton->getSkeleton(), resource, environment);
		instance->setReloadPolicy(SKELETON::SkeletalStateGraphInstance::ReloadPolicy::OwnerValidated);
		revision = resource->revision;
	}
	auto compileBindings = [&](const nlohmann::json& parameters, UTILS::Environment& schema) {
		std::vector<Binding> candidate;
		for (const auto& [name, p] : parameters.items()) {
			auto value = DATA::defaultValue(p.at("Type"));
			const auto expected = DATA::typeOf(value);
			if (auto f = std::get_if<float>(&value)) {
				schema.declare(name, *f);
			}
			else if (auto i = std::get_if<int>(&value)) {
				schema.declare(name, *i);
			}
			else if (auto flag = std::get_if<bool>(&value)) {
				schema.declare(name, *flag, p.at("Type") == "trigger");
			}
			if (!settings.Bindings.contains(name)) {
				throw std::runtime_error("Unbound graph parameter: " + name);
			}
			const auto slot = context->slot(settings.Bindings.at(name));
			if (context->type(slot) != expected) {
				throw std::runtime_error("Graph binding type mismatch: " + name);
			}
			candidate.push_back({ slot, schema.slot(name), expected });
		}
		return candidate;
	};
	if (resource->revision != revision && !instance->runtime()->metadata().value("LockTransitions", false)) {
		try {
			auto schema = *environment;
			auto candidate = compileBindings(resource->definition->document.at("Parameters"), schema);
			std::string error;
			if (!instance->runtime()->replace(resource->definition, error)) {
				throw std::runtime_error(error);
			}
			bindings = std::move(candidate);
			if (TESTHOOK::stateGraphReloaded) {
				TESTHOOK::stateGraphReloaded(instance->runtime()->state());
			}
		} catch (const std::exception& e) {
			std::cerr << "State graph component reload rejected: " << e.what() << std::endl;
		}
		revision = resource->revision;
	}
	if (!boundContext || boundContext.get() != context.get()) {
		auto schema = *environment;
		bindings = compileBindings(instance->runtime()->definition().at("Parameters"), schema);
		boundContext = UTILS::WeakPtr<ComponentBase>(static_cast<ComponentBase*>(context.get()));
		lastTick = 0;
	}
	animator->claimPlayback(*this);
	playbackTarget = UTILS::WeakPtr<ComponentBase>(static_cast<ComponentBase*>(animator.get()));
	if (animator->getPlayable() != instance.get()) {
		animator->setPlayable(instance);
	}
	if (paused || context->tick() == lastTick) {
		return;
	}
	for (const auto& b : bindings) {
		const auto& value = context->read(b.context);
		if (auto p = std::get_if<float>(&value)) {
			environment->set(b.parameter, *p);
		}
		else if (auto p = std::get_if<int>(&value)) {
			environment->set(b.parameter, *p);
		}
		else if (auto p = std::get_if<bool>(&value)) {
			environment->set(b.parameter, *p);
		}
	}
	if (!settings.PoseOffset.empty()) {
		instance->runtime()->setPoseOffset(std::get<MATH::Vector3f>(context->read(context->slot(settings.PoseOffset))));
	}
	instance->update(dt);
#if USE_PHYSICS_3D
	if (settings.ApplyRootMotion) {
		const auto& motion = instance->rootMotion();
		if (motion.valid) {
			if (TESTHOOK::rootMotionSampled) {
				TESTHOOK::rootMotionSampled(motion);
			}
			if (auto motor = Resolve<CharacterMotorComponent>(scene, settings.RootMotionMotor)) {
				const auto delta = MATH::QuaternionF(MATH::Vector3f { 0, motor->heading, 0 }) * motion.translationDeltaMeters;
				motor->motor.capsule.feet += delta;
			}
		}
	}
#endif
	for (const auto& event : instance->consumeAnimationEvents()) {
		if (TESTHOOK::animationEvent) {
			TESTHOOK::animationEvent(event.clip, event.name, event.timeSeconds);
		}
	}
	lastTick = context->tick();
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SkeletalStateGraphComponent>();
	return true;
}();
}
