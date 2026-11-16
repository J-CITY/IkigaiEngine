#include "../EcsDemo.h"
#include "../engine/ecsModule/world.h"

#include <iostream>

namespace Example {
void EcsDemo::run() {
	std::cout << "=== Starting ECS Demo ===\n";

	// A. Initialization
	IKIGAI::ECS::World world;
	auto compMgr = world.getComponentManager();
	auto sysMgr = world.getSystemManager();

	// B. Register Components
	// Position and Velocity share archetype columns. Health uses a sparse pool.
	compMgr->registerComponent<Position>(IKIGAI::ECS::StorageType::Archetype);
	compMgr->registerComponent<Velocity>(IKIGAI::ECS::StorageType::Archetype);
	compMgr->registerComponent<Health>(IKIGAI::ECS::StorageType::Sparse);

	// C. Singleton Component
	compMgr->addSingletonComponent<GlobalConfig>();
	auto config = compMgr->getSingletonComponent<GlobalConfig>();
	if (config) {
		std::cout << "Global Config Loaded from " << config->sAppName << "\n";
	}

	// D. Entity Creation
	using namespace IKIGAI::ECS;
	Entity e1 = world.createEntity();
	const auto position = compMgr->addComponent(e1, Position { 0, 0 });
	compMgr->addComponent(e1, Velocity { 1.0f, 0.5f });

	Entity e2 = world.createEntity();
	compMgr->addComponent(e2, Position { 10, 10 });
	compMgr->addComponent(e2, Velocity { -1.0f, -0.5f });
	compMgr->addComponent(e2, Health { 50 }); // Sparse component

	// E. Relations
	// e2 follows e1
	compMgr->getRelationManager().addRelation(e2, compMgr->getComponentType<Follows>(), e1);

	// Check Relations
	if (compMgr->getRelationManager().hasRelation(e2, compMgr->getComponentType<Follows>(), e1)) {
		std::cout << "Entity " << (int)e2.getUniqueId() << " follows " << (int)e1.getUniqueId() << "\n";
	}

	// F. Systems

	// 1. Add Class-based System
	sysMgr->addSystem(std::make_shared<MovementSystem>());

	// 1.5 Sparse Iteration Example
	// Efficiently iterates only entities that have Health (sparse), then checks/gets Position.

	std::cout << "--- Sparse Iteration Test ---\n";
	compMgr->forEachSparse<Health>(
		[&](Entity e, Health& hp) { std::cout << "Entity " << (int)e.getUniqueId() << " (Sparse Health) has HP: " << hp.hp << "\n"; });

	// 2. Add SystemBuilder System (Inline logic)
	// Defines a system that operates on Position and Health
	// TODO: need check for what component call forEach
	// world.system<Position, Health>("HealthRegenSystem")
	//      .onUpdate([&](Entity e, Position& pos, Health& hp, double dt) {
	//	     if (hp.hp < 100) {
	//		     hp.hp += 1;
	//		     // std::cout << "Entity " << e.getUniqueId() << " healed. HP: " << hp.hp << "\n";
	//	     }
	//      });

	// 3. System to process Relations
	// Note: Current simple forEach doesn't directly iterate relations.
	// We can iterate entities and check relations manually or use special queries if supported.
	// Here is a manual example:
	registerFollowerSystem(world);

	// G. Simulation Loop
	constexpr std::chrono::duration<double> dt { 1.0 / 60.0 };
	for (int i = 0; i < 5; ++i) {
		std::cout << "--- Frame " << i << " ---\n";
		sysMgr->runUpdate(dt);
		sysMgr->runLateUpdate(dt);
	}

	if (const auto* finalPosition = compMgr->resolveComponent(position)) {
		std::cout << "Entity 1 position: " << finalPosition->x << ", " << finalPosition->y << "\n";
	}

	std::cout << "=== ECS Demo Finished ===\n";
}
}
