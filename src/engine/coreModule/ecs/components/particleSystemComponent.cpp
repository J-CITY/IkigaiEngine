#include "particleSystemComponent.h"
#include "coreModule/ecs/object.h"
#include "resourceModule/particleManager.h"

namespace IKIGAI::ECS {
template <int D>
ParticleSystemComponent<D>::ParticleSystemComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ComponentBase(object)
	, mDescriptor(descriptor) {
	mDescriptor.Type = GetType<ParticleSystemComponent<D>>();
	mAsset = RESOURCES::ServiceManager::Get<RESOURCES::ParticleLoader>().loadResource(descriptor.EffectPath);
	mRuntime = std::make_unique<PARTICLES::Runtime<D>>(mAsset->definition, descriptor.Seed);
	for (const auto& [name, value] : descriptor.Parameters)
		mRuntime->setParameter(name, value);
	if (!descriptor.AutoPlay)
		mRuntime->pause();
}
template <int D>
ParticleSystemComponent<D>::ParticleSystemComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ParticleSystemComponent(object, static_cast<const Descriptor&>(descriptor)) { }
template <int D> ParticleSystemComponent<D>::~ParticleSystemComponent() = default;
template <int D>
ParticleSystemComponent<D>::ParticleSystemComponent(const ParticleSystemComponent& other)
	: ComponentBase(other)
	, mDescriptor(other.mDescriptor)
	, mAsset(other.mAsset)
	, mRuntime(other.mRuntime ? std::make_unique<PARTICLES::Runtime<D>>(*other.mRuntime) : nullptr) { }
template <int D> auto ParticleSystemComponent<D>::operator=(const ParticleSystemComponent& other) -> ParticleSystemComponent& {
	if (this != &other) {
		auto runtime = other.mRuntime ? std::make_unique<PARTICLES::Runtime<D>>(*other.mRuntime) : nullptr;
		ComponentBase::operator=(other);
		mDescriptor = other.mDescriptor;
		mAsset = other.mAsset;
		mRuntime = std::move(runtime);
	}
	return *this;
}
template <int D> auto ParticleSystemComponent<D>::getDescriptor() const -> Descriptor {
	auto d = mDescriptor;
	d.Type = GetType<ParticleSystemComponent<D>>();
	return d;
}
template <int D> void ParticleSystemComponent<D>::tick(double dt) {
	if (mRuntime)
		mRuntime->update(dt, obj->getTransform()->getTransform().getWorldMatrix());
}
template <int D> void ParticleSystemComponent<D>::play() {
	if (mRuntime)
		mRuntime->play();
}
template <int D> void ParticleSystemComponent<D>::pause() {
	if (mRuntime)
		mRuntime->pause();
}
template <int D> void ParticleSystemComponent<D>::resume() {
	if (mRuntime)
		mRuntime->resume();
}
template <int D> void ParticleSystemComponent<D>::stop(bool clear) {
	if (mRuntime)
		mRuntime->stop(clear);
}
template <int D> void ParticleSystemComponent<D>::restart(uint64_t seed) {
	mDescriptor.Seed = seed;
	if (mRuntime)
		mRuntime->restart(seed);
}
template <int D> void ParticleSystemComponent<D>::setParameter(const std::string& name, const std::vector<float>& value) {
	if (!mRuntime)
		throw std::logic_error("Particle effect not loaded");
	mRuntime->setParameter(name, value);
	mDescriptor.Parameters[name] = value;
}
template class ParticleSystemComponent<2>;
template class ParticleSystemComponent<3>;
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ParticleSystem2DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ParticleSystem3DComponent>();
	return true;
}();
}
