#pragma once

#include "../engine/ecsModule/componentManager.h"

#include <string>

namespace Example {

// Archetype component. Register explicitly with StorageType::Archetype.
// Good for components that most entities have and are accessed frequently together.
struct Position {
	float x = 0.0f;
	float y = 0.0f;
};

struct Velocity {
	float dx = 0.0f;
	float dy = 0.0f;
};

// Sparse component for entities that can take damage.
struct Health {
	int hp = 100;
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

}
