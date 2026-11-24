#pragma once
#if USE_PHYSICS_3D
#include "component.h"
#include "physicsModule/physicWorld.h"

namespace IKIGAI::ECS {
class PhysicsGrabComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		template <class C> static void serde(C& context, Descriptor& value) {
			serde::serde_struct(context, value).field(&Descriptor::Type, "PhysicsGrabComponentType");
		}
	};
	explicit PhysicsGrabComponent(UTILS::Ref<Object> owner);
	PhysicsGrabComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor&);
	Descriptor getDescriptor() const;
	void onUpdate(std::chrono::duration<double>) override;
	void onFixedUpdate(std::chrono::duration<double> dt) override;
	void onDisable() override;
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	std::weak_ptr<PHYSICS::World> mWorld {};
	PHYSICS::Id mBody = 0;
	PHYSICS::Vec3 mLocalAnchor {};
	PHYSICS::Vec3 mPlanePoint {};
	PHYSICS::Vec3 mPlaneNormal {};
	PHYSICS::Vec3 mTarget {};
};
template <> inline std::string GetType<PhysicsGrabComponent>() {
	return "class IKIGAI::ECS::PhysicsGrabComponent";
}
template <> inline std::string GetComponentName<PhysicsGrabComponent>() {
	return "PhysicsGrabComponent";
}
}
#endif
