#pragma once
#include "surfaceSettings.h"
#include <memory>

namespace IKIGAI::RENDER {
class MaterialInterface;
}
namespace IKIGAI::SURFACE {
void validateSurfaceMaterials(const Settings& settings, const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& sources);
std::shared_ptr<RENDER::MaterialInterface> buildSurfaceMaterial(const Settings& settings,
	const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& sources, const std::vector<uint8_t>* coveragePixels = nullptr);
}
