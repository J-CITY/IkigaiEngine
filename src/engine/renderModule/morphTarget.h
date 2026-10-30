#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "mathModule/math.h"

namespace IKIGAI::RENDER {
struct MorphVertexDelta {
	MATH::Vector4f Position { 0, 0, 0, 0 };
	MATH::Vector4f Normal { 0, 0, 0, 0 };
	MATH::Vector4f Tangent { 0, 0, 0, 0 };
};
static_assert(sizeof(MorphVertexDelta) == 3 * sizeof(MATH::Vector4f));

struct MorphTarget {
	std::string Name {};
	float DefaultWeight = 0;
	std::vector<MorphVertexDelta> Deltas {};
};

struct MorphAnimationKey {
	float Time = 0;
	float Weight = 0;
};

struct MorphAnimationTrack {
	std::size_t MeshIndex = 0;
	std::size_t TargetIndex = 0;
	std::vector<MorphAnimationKey> Keys {};
};

struct MorphAnimationClip {
	std::string Name {};
	float Duration = 0;
	std::vector<MorphAnimationTrack> Tracks {};
};
}
