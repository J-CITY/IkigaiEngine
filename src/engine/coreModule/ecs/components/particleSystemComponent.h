#pragma once
#include "component.h"
#include "particleModule/particles.h"

namespace IKIGAI::RESOURCES {
struct ParticleAsset;
}
namespace IKIGAI::ECS {
template <int D> class ParticleSystemComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type, EffectPath;
		bool AutoPlay = true;
		uint64_t Seed = 1;
		PARTICLES::Parameters Parameters;
		template <class C> static auto serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, D == 2 ? "ParticleSystem2DComponentType" : "ParticleSystem3DComponentType")
				.field(&Descriptor::EffectPath, "EffectPath")
				.field(&Descriptor::AutoPlay, "AutoPlay", default_ { true })
				.field(&Descriptor::Seed, "Seed", default_ { uint64_t(1) })
				.field(&Descriptor::Parameters, "Parameters", default_ { PARTICLES::Parameters {} });
		}
	};
	ParticleSystemComponent(UTILS::Ref<Object> object, const Descriptor& descriptor);
	ParticleSystemComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	explicit ParticleSystemComponent(UTILS::Ref<Object> object)
		: ComponentBase(object) { }
	~ParticleSystemComponent() override;
	ParticleSystemComponent(const ParticleSystemComponent&);
	ParticleSystemComponent& operator=(const ParticleSystemComponent&);
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void tick(double dt);
	void play();
	void pause();
	void resume();
	void stop(bool clear = false);
	void restart(uint64_t seed);
	void setParameter(const std::string& name, const std::vector<float>& value);
	void setRuntimeParameter(const std::string& name, const std::vector<float>& value);
	void setEventSpawnsEnabled(bool enabled);
	bool isComplete() const {
		return !mRuntime || mRuntime->isComplete();
	}
	bool isAlive() const {
		return mRuntime && mRuntime->isAlive();
	}
	const auto* runtime() const {
		return mRuntime.get();
	}
	const auto& asset() const {
		return mAsset;
	}

private:
	friend class ParticleSimulationSystem;

	Descriptor mDescriptor;
	std::shared_ptr<RESOURCES::ParticleAsset> mAsset;
	std::unique_ptr<PARTICLES::Runtime<D>> mRuntime;
};
using ParticleSystem2DComponent = ParticleSystemComponent<2>;
using ParticleSystem3DComponent = ParticleSystemComponent<3>;
template <> inline std::string GetType<ParticleSystem2DComponent>() {
	return "class IKIGAI::ECS::ParticleSystem2DComponent";
}
template <> inline std::string GetType<ParticleSystem3DComponent>() {
	return "class IKIGAI::ECS::ParticleSystem3DComponent";
}
template <> inline std::string GetComponentName<ParticleSystem2DComponent>() {
	return "ParticleSystem2DComponent";
}
template <> inline std::string GetComponentName<ParticleSystem3DComponent>() {
	return "ParticleSystem3DComponent";
}
extern template class ParticleSystemComponent<2>;
extern template class ParticleSystemComponent<3>;
}
