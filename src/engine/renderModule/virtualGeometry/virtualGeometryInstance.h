#pragma once

#include "mathModule/math.h"
#include "virtualGeometryModule/runtime/virtualMeshDebugMode.h"

#include <cstdint>
#include <memory>
#include <span>

namespace IKIGAI::RENDER {
class MaterialInterface;
class ModelInterface;
}

namespace IKIGAI::VG {
class VirtualMeshAsset;

inline std::shared_ptr<RENDER::MaterialInterface> ResolveVirtualMeshMaterial(
	std::span<const std::shared_ptr<RENDER::MaterialInterface>> materials, uint32_t slot) {
	if (slot < materials.size() && materials[slot]) {
		return materials[slot];
	}
	return materials.empty() ? nullptr : materials.front();
}

// A visible, active instance for one frame. Material slots are borrowed through synchronous preparation only.
struct VirtualGeometryInstance {
	std::shared_ptr<VirtualMeshAsset> Asset {};
	std::shared_ptr<RENDER::ModelInterface> Fallback {};
	std::span<const std::shared_ptr<RENDER::MaterialInterface>> Materials {};
	MATH::Matrix4f World = MATH::Matrix4f::Identity;
	MATH::Matrix4f PreviousWorld = MATH::Matrix4f::Identity;
	MATH::Vector3f Position {};
	uint64_t InstanceId = 0;
	float ErrorThreshold = 1.0f;
	VirtualMeshDebugMode DebugMode = VirtualMeshDebugMode::NONE;
	bool ContinuousHistory = false;
	bool AllowClusterShadows = false;
};
}
