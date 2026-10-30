#pragma once
#include "weather.h"

namespace IKIGAI::ENVIRONMENT {
inline constexpr unsigned SurfaceWeatherGridSize = 16;

struct alignas(16) SurfaceWeatherGpu {
	MATH::Vector4f Grid = { 0, 0, 1, 0 };
	MATH::Vector4f Projection = { 0, 0, 0, float(SurfaceWeatherGridSize) };
	std::array<MATH::Vector4f, SurfaceWeatherGridSize * SurfaceWeatherGridSize> Cells = {};
};
static_assert(sizeof(SurfaceWeatherGpu) == 4128);

class SurfaceWeatherRuntime {
public:
	void update(const Settings& settings, const GpuState& state, MATH::Vector3f anchor, float dt, const WeatherRuntime::Trace& trace);
	const SurfaceWeatherGpu& gpu() const;
	unsigned queries() const;

private:
	SurfaceWeatherGpu mGpu = {};
	int mOriginX = 0;
	int mOriginZ = 0;
	size_t mCursor = 0;
	unsigned mQueries = 0;
	bool mInitialized = false;
};
}
