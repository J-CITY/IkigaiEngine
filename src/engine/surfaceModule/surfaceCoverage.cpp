#include "surfaceCoverage.h"
#include "surfaceGenerator.h"
#include "surfaceJob.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::SURFACE {
namespace {
	float smooth(float low, float high, float value) {
		if (high <= low) {
			return value >= low ? 1.0f : 0.0f;
		}
		const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
		return t * t * (3 - 2 * t);
	}
	float range(float low, float high, float transition, float value) {
		if (transition == 0) {
			return value >= low && value <= high ? 1.0f : 0.0f;
		}
		return smooth(low - transition, low, value) * (1 - smooth(high, high + transition, value));
	}
	std::array<uint8_t, 4> gridPoint(const Settings& settings, unsigned ix, unsigned iz) {
		const float u = float(ix) / float(settings.Blend.MapResolution - 1);
		const float v = float(iz) / float(settings.Blend.MapResolution - 1);
		const float x = (u - .5f) * settings.TilesX * settings.TileSize;
		const float z = (v - .5f) * settings.TilesZ * settings.TileSize;
		const float step = settings.TileSize / float(settings.Resolution);
		const auto n = MATH::Vector3f::Normalize({ height(settings, x - step, z) - height(settings, x + step, z), 2 * step,
			height(settings, x, z - step) - height(settings, x, z + step) });
		const float slope = MATH::TO_DEGREES(std::acos(std::clamp(n.y, -1.0f, 1.0f)));
		NoiseSettings boundary;
		boundary.Seed = settings.Noise.Seed ^ 0x9876u;
		boundary.Amplitude = settings.Blend.BoundaryNoise;
		boundary.Frequency = settings.Blend.NoiseFrequency;
		boundary.Octaves = 2;
		const float elevation = height(settings, x, z) + height(boundary, x, z);
		Coverage result;
		if (settings.Blend.Enabled) {
			for (unsigned index = 1; index < settings.Layers.size(); ++index) {
				const auto& layer = settings.Layers[index];
				const float weight = range(layer.MinHeight, layer.MaxHeight, layer.HeightTransition, elevation)
					* range(layer.MinSlope, layer.MaxSlope, layer.SlopeTransition, slope);
				for (auto& current : result.Weights) {
					current *= 1 - weight;
				}
				result.Weights[index] += weight;
			}
		}
		for (const auto& edit : settings.Edits) {
			if (!edit.Enabled || edit.Opacity == 0) {
				continue;
			}
			for (const auto& stroke : edit.Strokes) {
				const float weight = edit.Opacity * brushInfluence(stroke, x, z);
				if (stroke.Operation == BrushOperation::FoliageDensity) {
					result.FoliageDensity = std::lerp(result.FoliageDensity, stroke.Value, weight);
				}
				else if (stroke.Operation == BrushOperation::PaintLayer && weight > 0) {
					const unsigned target = settings.layerIndex(stroke.LayerId);
					for (auto& current : result.Weights) {
						current *= 1 - weight;
					}
					result.Weights[target] += weight;
				}
			}
		}
		std::array<uint8_t, 4> encoded = {};
		float cumulative = result.Weights[0];
		for (unsigned index = 1; index < 4; ++index) {
			cumulative += result.Weights[index];
			encoded[index - 1] = uint8_t(std::lround((cumulative > .000001f ? result.Weights[index] / cumulative : 0) * 255));
		}
		encoded[3] = uint8_t(std::lround(result.FoliageDensity * 255));
		return encoded;
	}
}
std::vector<uint8_t> buildCoverageMap(const Settings& settings, const std::atomic_bool* cancellation) {
	settings.validate();
	const unsigned resolution = settings.Blend.MapResolution;
	std::vector<uint8_t> result(size_t(resolution) * resolution * 4);
	for (unsigned z = 0; z < resolution; ++z) {
		checkBuildCancellation(cancellation);
		for (unsigned x = 0; x < resolution; ++x) {
			const auto value = gridPoint(settings, x, z);
			std::copy(value.begin(), value.end(), result.begin() + (size_t(z) * resolution + x) * 4);
		}
	}
	return result;
}
Coverage coverage(const Settings& settings, float x, float z) {
	const float extent = float(settings.Blend.MapResolution - 1);
	const float gx = std::clamp((x / (settings.TilesX * settings.TileSize) + .5f) * extent, 0.0f, extent);
	const float gz = std::clamp((z / (settings.TilesZ * settings.TileSize) + .5f) * extent, 0.0f, extent);
	const unsigned ix = unsigned(gx);
	const unsigned iz = unsigned(gz);
	const unsigned nx = std::min(ix + 1, settings.Blend.MapResolution - 1);
	const unsigned nz = std::min(iz + 1, settings.Blend.MapResolution - 1);
	const auto a = gridPoint(settings, ix, iz);
	const auto b = gridPoint(settings, nx, iz);
	const auto c = gridPoint(settings, ix, nz);
	const auto d = gridPoint(settings, nx, nz);
	Coverage result;
	for (unsigned index = 0; index < 4; ++index) {
		const float value
			= std::lerp(std::lerp(float(a[index]), float(b[index]), gx - ix), std::lerp(float(c[index]), float(d[index]), gx - ix), gz - iz) / 255;
		if (index == 3) {
			result.FoliageDensity = value;
		}
		else {
			for (auto& weight : result.Weights) {
				weight *= 1 - value;
			}
			result.Weights[index + 1] += value;
		}
	}
	return result;
}
MATH::Vector4f coverageUv(const Settings& settings) {
	const float scale = float(settings.Blend.MapResolution - 1) / settings.Blend.MapResolution;
	return { scale / (settings.TilesX * settings.TileSize), scale / (settings.TilesZ * settings.TileSize), .5f, .5f };
}
}
