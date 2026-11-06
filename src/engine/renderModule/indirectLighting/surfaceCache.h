#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace IKIGAI::RENDER {
inline constexpr int SurfaceCacheMaxCards = 16;
inline constexpr int SurfaceCacheCardSide = 8;
inline constexpr auto SurfaceCacheRelightStageName = "SPSurfaceRelight";

struct SurfaceCacheCardGpu {
	std::array<float, 4> OriginTolerance {};
	std::array<float, 4> AxisU {};
	std::array<float, 4> AxisV {};
	std::array<float, 4> AlbedoRoughness {};
	std::array<float, 4> Emissive {};
};
static_assert(sizeof(SurfaceCacheCardGpu) == 80);

struct SurfaceCacheGpu {
	std::array<int32_t, 4> State {};
	std::array<SurfaceCacheCardGpu, SurfaceCacheMaxCards> Cards {};
};
static_assert(sizeof(SurfaceCacheGpu) == 1296);

struct SurfaceCacheConfig {
	std::string Scene;
	SurfaceCacheGpu Gpu {};
};
}
