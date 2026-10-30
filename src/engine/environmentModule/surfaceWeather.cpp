#include "surfaceWeather.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ENVIRONMENT {
void SurfaceWeatherRuntime::update(
	const Settings& settings, const GpuState& state, MATH::Vector3f anchor, float dt, const WeatherRuntime::Trace& trace) {
	mQueries = 0;
	dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, .25f) : 0;
	if (!trace || settings.SurfaceWeatherQueryBudget == 0 || !std::isfinite(anchor.x) || !std::isfinite(anchor.y) || !std::isfinite(anchor.z)) {
		mGpu.Grid.w = 0;
		mInitialized = false;
		return;
	}
	const float cellSize = settings.SurfaceWeatherRadius * 2 / SurfaceWeatherGridSize;
	const bool snow = state.WeatherState.y > state.WeatherState.x;
	const float speed = snow ? settings.SnowSpeed : settings.RainSpeed;
	const float windResponse = snow ? .9f : .5f;
	const MATH::Vector4f projection { state.Wind.x * windResponse / speed, state.Wind.y * windResponse / speed, 0, float(SurfaceWeatherGridSize) };
	const int originX = int(std::floor((anchor.x + projection.x * anchor.y) / cellSize)) - int(SurfaceWeatherGridSize / 2);
	const int originZ = int(std::floor((anchor.z + projection.y * anchor.y) / cellSize)) - int(SurfaceWeatherGridSize / 2);
	if (!mInitialized || std::abs(cellSize - mGpu.Grid.z) > 1e-4f) {
		mGpu.Cells = {};
		mCursor = 0;
	}
	else if (originX != mOriginX || originZ != mOriginZ) {
		const auto previous = mGpu.Cells;
		mGpu.Cells = {};
		for (int z = 0; z < int(SurfaceWeatherGridSize); ++z) {
			for (int x = 0; x < int(SurfaceWeatherGridSize); ++x) {
				const int oldX = originX + x - mOriginX;
				const int oldZ = originZ + z - mOriginZ;
				if (oldX >= 0 && oldZ >= 0 && oldX < int(SurfaceWeatherGridSize) && oldZ < int(SurfaceWeatherGridSize)) {
					mGpu.Cells[z * SurfaceWeatherGridSize + x] = previous[oldZ * SurfaceWeatherGridSize + oldX];
				}
			}
		}
		mCursor = 0;
	}
	// A changed precipitation ray must be checked again before it can wet a surface.
	if (std::abs(projection.x - mGpu.Projection.x) > .01f || std::abs(projection.y - mGpu.Projection.y) > .01f || projection.z != mGpu.Projection.z) {
		for (auto& cell : mGpu.Cells) {
			cell.w = 0;
		}
		mCursor = 0;
	}
	mOriginX = originX;
	mOriginZ = originZ;
	mInitialized = true;
	mGpu.Grid = { originX * cellSize, originZ * cellSize, cellSize, 1 };
	mGpu.Projection = projection;
	const float topHeight = std::ceil((anchor.y + settings.PrecipitationHeight) / 4) * 4;
	const float traceHeight = std::max(settings.PrecipitationHeight * 2, 64.0f);
	const auto budget = std::min(settings.SurfaceWeatherQueryBudget, unsigned(mGpu.Cells.size()));
	for (unsigned query = 0; query < budget; ++query) {
		const auto index = mCursor++ % mGpu.Cells.size();
		const unsigned x = unsigned(index) % SurfaceWeatherGridSize;
		const unsigned z = unsigned(index) / SurfaceWeatherGridSize;
		const MATH::Vector3f top { mGpu.Grid.x + (float(x) + .5f) * cellSize - projection.x * topHeight, topHeight,
			mGpu.Grid.y + (float(z) + .5f) * cellSize - projection.y * topHeight };
		const auto hit = trace(top, { projection.x * traceHeight, -traceHeight, projection.y * traceHeight });
		++mQueries;
		const float height = hit ? hit->Position.y : -1000000.0f;
		auto& cell = mGpu.Cells[index];
		if (std::abs(cell.x - height) > .25f) {
			cell.y = 0;
			cell.z = 0;
		}
		cell.x = height;
		cell.w = 1;
	}
	for (auto& cell : mGpu.Cells) {
		if (cell.w < .5f) {
			continue;
		}
		cell.y
			= std::clamp(cell.y + (state.WeatherState.x * settings.WetnessRate - settings.DryingRate * (1 - state.WeatherState.x)) * dt, 0.0f, 1.0f);
		cell.z = std::clamp(
			cell.z + (state.WeatherState.y * settings.SnowAccumulationRate - settings.SnowMeltRate * (1 - state.WeatherState.y)) * dt, 0.0f, 1.0f);
	}
}
const SurfaceWeatherGpu& SurfaceWeatherRuntime::gpu() const {
	return mGpu;
}
unsigned SurfaceWeatherRuntime::queries() const {
	return mQueries;
}
}
