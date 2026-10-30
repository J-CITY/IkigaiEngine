#include "instanceBatch.h"
#include "render.h"
#include "resourceModule/serviceManager.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
InstanceBatch::InstanceBatch(std::vector<InstanceTransform> transforms)
	: mTransforms(std::move(transforms)) {
	if (mTransforms.empty() || mTransforms.size() > 20000) {
		throw std::invalid_argument("An instance batch requires 1 to 20000 transforms");
	}
}
const std::vector<InstanceTransform>& InstanceBatch::transforms() const {
	return mTransforms;
}
std::shared_ptr<StorageBufferInterface> InstanceBatch::buffer() {
	if (!mBuffer) {
		std::vector<InstanceTransform> gpu;
		gpu.reserve(mTransforms.size());
		for (const auto& transform : mTransforms) {
			gpu.push_back({ MATH::Matrix4f::Transpose(transform.World), MATH::Matrix4f::Transpose(transform.PreviousWorld), transform.Tint });
		}
		mBuffer = RESOURCES::ServiceManager::Get<Renderer>().createStorageBuffer(gpu.data(), gpu.size(), sizeof(InstanceTransform));
	}
	return mBuffer;
}
static_assert(sizeof(InstanceTransform) == 144);
static_assert(sizeof(InstanceMetaGpu) == 32);
}
