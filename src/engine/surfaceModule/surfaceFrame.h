#pragma once
#include "mathModule/math.h"

namespace IKIGAI::SURFACE {
struct FrameState {
	unsigned Budget = 2;
	MATH::Matrix4f LastWorld = {};
	MATH::Matrix4f PreviousWorld = {};
	bool Initialized = false;
	void beginFrame(unsigned budget);
	const MATH::Matrix4f& previous(const MATH::Matrix4f& world);
};
}
