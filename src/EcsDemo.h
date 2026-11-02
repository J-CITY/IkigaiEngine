#pragma once
#include "engine/ecsModule/componentManager.h"
#include "engine/ecsModule/systemBuilder.h"
#include "engine/ecsModule/systemManager.h"
#include "engine/ecsModule/world.h"
#include <iostream>
#include <string>

namespace IKIGAI::ECS {
class Object;
}

namespace Example {

// Implementations live in EcsDemo.cpp. Register extensions before loading trees.
void RegisterBehaviorTreeTaskExample();
void ConfigureBehaviorTree(IKIGAI::ECS::Object& npc, int candidateObjectId, bool controlMotor = true);

// 1. Components Definition
// ------------------------

// Archetype component. Register explicitly with StorageType::Archetype.
// Good for components that most entities have and are accessed frequently together.
struct Position : public IKIGAI::ECS::Component {
	float x = 0.0f, y = 0.0f;
	Position() = default;
	Position(float _x, float _y)
		: Component()
		, x(_x)
		, y(_y) { }
};

struct Velocity : public IKIGAI::ECS::Component {
	float dx = 0.0f, dy = 0.0f;
	Velocity() = default;
	Velocity(float _dx, float _dy)
		: Component()
		, dx(_dx)
		, dy(_dy) { }
};

// Sparse Component
// Good for components that few entities have (saves memory compared to archetype gaps)
// or are added/removed frequently.
struct Health : public IKIGAI::ECS::Component {
	int hp = 100;
	Health(int i)
		: Component()
		, hp(i) { };
};

// Singleton Component
// Only one instance per ComponentManager. Good for global config or state.
struct GlobalConfig : public IKIGAI::ECS::Component {
	float gravity = 9.81f;
	std::string sAppName = "ECS Demo";
};

// Tag Component for Relations
// Used as a type for the relation itself (e.g., "Parent", "Owner")
struct Parent : public IKIGAI::ECS::Component { };
struct Follows : public IKIGAI::ECS::Component { };

// 2. Class-based System
// ---------------------
// Standard way to define logic.
class MovementSystem : public IKIGAI::ECS::System {
public:
	// Constructor can set name and Dependencies
	MovementSystem() {
		mName = "MovementSystem";
	}

	void onUpdate(IKIGAI::ECS::World& world, IKIGAI::ECS::CommandBuffer&, std::chrono::duration<double> dt) override {
		// Iterate over all entities with Position and Velocity
		world.getComponentManager()->forEach<Position, Velocity>([&](IKIGAI::ECS::Entity e, Position& pos, Velocity& vel) {
			pos.x += vel.dx * static_cast<float>(dt.count());
			pos.y += vel.dy * static_cast<float>(dt.count());
			// std::cout << "Entity " << e.getUniqueId() << " moved to " << pos.x << ", " << pos.y << "\n";
		});
	}
};

class EcsDemo {
public:
	static void run() {
		std::cout << "=== Starting ECS Demo ===\n";

		// A. Initialization
		IKIGAI::ECS::World world;
		auto compMgr = world.getComponentManager();
		auto sysMgr = world.getSystemManager();

		// B. Register Components
		// Unregistered components default to Sparse. Position and Velocity stay archetype columns.
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
		compMgr->addComponent(e1, Position { 0, 0 });
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
		world.system<Position, Velocity>("FollowerSystem").onUpdate([&](Entity e, Position& pos, Velocity& vel, std::chrono::duration<double> dt) {
			auto& relMgr = world.getComponentManager()->getRelationManager();
			// Get targets for 'Follows' relation
			auto targets = relMgr.getTargets(e, world.getComponentManager()->getComponentType<Follows>());
			for (auto target : targets) {
				auto targetPos = world.getComponentManager()->getComponent<Position>(target);
				if (targetPos) {
					// Simple follow logic: move towards target
					float dx = targetPos->x - pos.x;
					float dy = targetPos->y - pos.y;
					// Normalize and set velocity (simplified)
					vel.dx = dx * 0.1f;
					vel.dy = dy * 0.1f;
				}
			}
		});

		// G. Simulation Loop
		std::chrono::duration<double> dt = std::chrono::duration<double>(16); // 60 FPS
		for (int i = 0; i < 5; ++i) {
			std::cout << "--- Frame " << i << " ---\n";
			sysMgr->runUpdate(dt);
			sysMgr->runLateUpdate(dt);
		}

		std::cout << "=== ECS Demo Finished ===\n";
	}
};
}
