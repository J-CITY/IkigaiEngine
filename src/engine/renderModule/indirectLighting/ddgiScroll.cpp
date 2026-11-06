#include "ddgiScroll.h"

#include <algorithm>
#include <cmath>

namespace IKIGAI::RENDER {
DdgiScrollGpu PlanDdgiScroll(DdgiScrollRuntime& state, std::array<float, 3> camera, int budget, bool reset) {
	const auto& volume = state.Volume;
	const std::array counts { volume.CountX, volume.CountY, volume.CountZ };
	const std::array anchor { volume.OriginX, volume.OriginY, volume.OriginZ };
	const int plane = counts[0] * counts[1];
	const int count = plane * counts[2];
	DdgiScrollGpu result;
	result.OriginSpacing[3] = volume.Spacing;
	result.Counts = { volume.CountX, volume.CountY, volume.CountZ, volume.Rays };
	bool reuse = state.Initialized && !reset;
	for (size_t axis = 0; axis < counts.size(); ++axis) {
		const double grid = (double(camera[axis]) - anchor[axis]) / volume.Spacing;
		const int cell = std::isfinite(grid) ? static_cast<int>(std::clamp(std::floor(grid + 0.5), -1048576.0, 1048576.0)) - (counts[axis] - 1) / 2
											 : state.Cell[axis];
		result.Delta[axis] = cell - state.Cell[axis];
		result.OriginSpacing[axis] = anchor[axis] + float(cell) * volume.Spacing;
		reuse &= std::abs(result.Delta[axis]) < counts[axis];
		state.Cell[axis] = cell;
	}
	result.Delta[3] = reuse ? 1 : 0;
	const auto previous = state.Valid;
	state.Valid.fill(false);
	for (int probe = 0; probe < count; ++probe) {
		result.Rows[probe][0] = -1;
		const int x = probe % counts[0] + result.Delta[0];
		const int y = probe / counts[0] % counts[1] + result.Delta[1];
		const int z = probe / plane + result.Delta[2];
		if (reuse && x >= 0 && x < counts[0] && y >= 0 && y < counts[1] && z >= 0 && z < counts[2]) {
			state.Valid[probe] = previous[x + y * counts[0] + z * plane];
		}
	}
	int row = 0;
	for (bool valid : { false, true }) {
		const int start = state.NextProbe;
		for (int offset = 0; offset < count && row < budget; ++offset) {
			const int probe = (start + offset) % count;
			if (state.Valid[probe] != valid || result.Rows[probe][0] >= 0) {
				continue;
			}
			result.Rows[probe][0] = row;
			result.Rows[row][1] = probe;
			state.Valid[probe] = true;
			state.NextProbe = (probe + 1) % count;
			++row;
		}
	}
	state.Initialized = true;
	return result;
}
}
