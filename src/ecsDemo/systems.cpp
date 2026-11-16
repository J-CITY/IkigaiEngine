#include "systems.h"
#include "../engine/ecsModule/world.h"
#include "components.h"

namespace Example {
MovementSystem::MovementSystem() {
	mName = "MovementSystem";
}

void MovementSystem::onUpdate(IKIGAI::ECS::World& world, IKIGAI::ECS::CommandBuffer&, std::chrono::duration<double> dt) {
	// Iterate over all entities with Position and Velocity
	world.getComponentManager()->forEach<IKIGAI::ECS::Write<Position>, IKIGAI::ECS::Read<Velocity>>(
		[&](IKIGAI::ECS::Entity e, Position& pos, const Velocity& vel) {
			pos.x += vel.dx * static_cast<float>(dt.count());
			pos.y += vel.dy * static_cast<float>(dt.count());
			// std::cout << "Entity " << e.getUniqueId() << " moved to " << pos.x << ", " << pos.y << "\n";
		});
}

void registerFollowerSystem(IKIGAI::ECS::World& world) {
	using namespace IKIGAI::ECS;
	auto& components = *world.getComponentManager();
	const auto follows = components.getComponentType<Follows>();
	world.system<Read<Position>, Write<Velocity>>("FollowerSystem")
		.onUpdate([&components, follows](Entity e, const Position& pos, Velocity& vel, std::chrono::duration<double>) {
			components.getRelationManager().forEachTarget(e, follows, [&](Entity target) {
				if (const auto* targetPos = components.tryGetComponent<Position>(target)) {
					vel.dx = (targetPos->x - pos.x) * 0.1f;
					vel.dy = (targetPos->y - pos.y) * 0.1f;
				}
			});
		});
}
}
