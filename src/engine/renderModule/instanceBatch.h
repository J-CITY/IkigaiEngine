#pragma once
#include "mathModule/math.h"
#include <memory>
#include <vector>

namespace IKIGAI::RENDER {
class StorageBufferInterface;
struct InstanceTransform {
	MATH::Matrix4f World = MATH::Matrix4f::Identity;
	MATH::Matrix4f PreviousWorld = MATH::Matrix4f::Identity;
	MATH::Vector4f Tint = { 1, 1, 1, 1 };
};
struct InstanceMetaGpu {
	MATH::Vector4f Parameters = {};
	MATH::Vector4f Tint = { 1, 1, 1, 1 };
};
// Immutable draw payload. CPU transforms also remain available to fallback and RT paths.
class InstanceBatch {
public:
	explicit InstanceBatch(std::vector<InstanceTransform> transforms);
	const std::vector<InstanceTransform>& transforms() const;
	std::shared_ptr<StorageBufferInterface> buffer();

private:
	std::vector<InstanceTransform> mTransforms = {};
	std::shared_ptr<StorageBufferInterface> mBuffer = {};
};
}
