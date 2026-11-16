#pragma once

namespace IKIGAI::TEST::ECS_CASES {

struct Position {
	int value = 0;
};

struct Velocity {
	int value = 0;
};

struct Health {
	int value = 0;
};

struct Shield {
	int value = 0;
};

struct Follows { };
struct Owns { };

struct alignas(16) AlignedPosition {
	int value = 0;
};

struct alignas(32) AlignedVelocity {
	int value = 0;
};

}
