#pragma once

#include "component.h"
#include <functional>
#include <renderModule/objects/boundingSphere.h>
#include <physicsModule/body.h>
#include "physicsModule/narrowPhase.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI
{
	namespace PHYSICS
	{
		class CollisionPrimitive;
	}
}

namespace IKIGAI::ECS { class Object; }

namespace IKIGAI::ECS {
	enum CollisionType {
		NONE = -1,
		SPHERE,
		AABB,
		OBB,
		COUNT,
	};
	IKI_CLASS(Groups=[Component])
	class PhysicsComponent : public ComponentBase {
		IKI_GENERATED_BODY(PhysicsComponent)
	public:
		//TODO: add params
		IKI_CLASS(Name=PhysicsComponent::Descriptor)
		struct Descriptor : public ComponentBase::Descriptor {
			IKI_GENERATED_BODY(PhysicsComponent::Descriptor)
			IKI_PROPERTY(SEREALIZE(name="PhysicsComponentType"))
			std::string Type;
		};

		PhysicsComponent(UTILS::Ref<ECS::Object> obj);
		PhysicsComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor) :
			PhysicsComponent(obj) {
		};
		int GetDirty() const { return m_dirtyFlag; }
		void SetDirty(bool dirtyFlag) { m_dirtyFlag = dirtyFlag; }
		void ResetDirty() { m_dirtyFlag = false; }
		std::shared_ptr<PHYSICS::RigidBody> body;
		IKIGAI::RENDER::BoundingSphere boundingSphere;

		std::shared_ptr<PHYSICS::CollisionPrimitive> collider;
		//std::unique_ptr<PHYSICS::CollisionPrimitive> collider;

		CollisionType collisionType = CollisionType::NONE;

		[[nodiscard]] Descriptor getDescriptor() const;
	private:
		bool m_dirtyFlag = false;

	public:
		static auto GetMembers() {
			return std::tuple{
			};
		}
	};

	template <>
	inline std::string ECS::GetType<PhysicsComponent>() {
		return "class IKIGAI::ECS::PhysicsComponent";
	}

	template <>
	inline std::string IKIGAI::ECS::GetComponentName<PhysicsComponent>() {
		return "PhysicsComponent";
	}
}

#include "generated/physicsComponent.generated.h"
