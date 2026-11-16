#pragma once

namespace ECS_BENCHMARK {

template <int Alignment> struct alignas(Alignment) Position {
	float x = 0.0f;
	float y = 0.0f;
};

template <int Alignment> struct alignas(Alignment) Velocity {
	float x = 0.0f;
	float y = 0.0f;
};

struct Marker {
	int value = 0;
};

template <int Index> struct Tag { };

}
