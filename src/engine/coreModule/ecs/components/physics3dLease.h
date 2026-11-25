#pragma once
#if USE_PHYSICS_3D
#include "physicsModule/physicWorld.h"
#include <memory>
#include <string>
namespace IKIGAI::ECS {
struct Physics3DLease {
	enum class Kind { Body, Soft, Fluid, Field };
	std::weak_ptr<PHYSICS::World> world;
	PHYSICS::Id id = 0;
	Kind kind = Kind::Body;
	std::string signature;
	PHYSICS::Vec3 particleFramePosition {};
	MATH::QuaternionF particleFrameRotation { 0, 0, 0, 1 };
	~Physics3DLease() {
		if (!id) {
			return;
		}
		if (auto w = world.lock()) {
			switch (kind) {
			case Kind::Body:
				w->destroyBody(id);
				break;
			case Kind::Soft:
				w->destroySoftBody(id);
				break;
			case Kind::Fluid:
				w->destroyFluid(id);
				break;
			case Kind::Field:
				w->forces.remove(id);
				break;
			}
		}
	}
};
}

#endif // USE_PHYSICS_3D
