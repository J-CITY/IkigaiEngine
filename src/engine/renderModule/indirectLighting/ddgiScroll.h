#pragma once

#include "renderModule/historyValidity.h"
#include "renderModule/renderGraph/screenProbeSettings.h"

#include <array>
#include <cstdint>

namespace IKIGAI::RENDER {
inline constexpr int MaxScrollingDdgiProbes = 256;

struct DdgiScrollGpu {
	std::array<float, 4> OriginSpacing {};
	// Current origin minus previous origin in cells. W is -1 for no trace, 0 for reset, 1 for reuse.
	std::array<int32_t, 4> Delta {};
	std::array<int32_t, 4> Counts {};
	// X maps a logical probe to a ray row (-1 when not scheduled). Y maps a ray row back to a probe.
	std::array<std::array<int32_t, 4>, MaxScrollingDdgiProbes> Rows {};
};
static_assert(sizeof(DdgiScrollGpu) == 4144);

struct DdgiScrollRuntime {
	ScreenProbeDdgiVolume Volume {};
	ScreenProbeGpuSettings ScreenProbes {};
	std::array<int, 3> Cell {};
	std::array<bool, MaxScrollingDdgiProbes> Valid {};
	std::array<uint64_t, HistoryReasonCount> Resets {};
	int NextProbe = 0;
	bool Initialized = false;
};

DdgiScrollGpu PlanDdgiScroll(DdgiScrollRuntime& state, std::array<float, 3> camera, int budget, bool reset);
}
