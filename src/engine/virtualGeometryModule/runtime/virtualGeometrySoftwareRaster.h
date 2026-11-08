#pragma once

#include <cstdint>
#include <memory>

namespace IKIGAI::RENDER {
class Renderer;
class ShaderInterface;
class StorageBufferInterface;
class UniformBufferInterface;
}

namespace IKIGAI::VG {
class VirtualGeometrySoftwareRaster final {
public:
	struct Input {
		uint32_t FirstDraw = 0;
		uint32_t DrawCount = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::shared_ptr<RENDER::StorageBufferInterface> Vertices {};
		std::shared_ptr<RENDER::StorageBufferInterface> Indices {};
		std::shared_ptr<RENDER::StorageBufferInterface> Clusters {};
		std::shared_ptr<RENDER::StorageBufferInterface> DrawRecords {};
		std::shared_ptr<RENDER::StorageBufferInterface> Instances {};
		std::shared_ptr<RENDER::UniformBufferInterface> EngineUbo {};
	};

	// The input is the final resident visibility batch with culling disabled.
	// SW admission requires unit clip w and constant depth at all three vertices.
	// On success, countBuffer() contains the batch-relative HW prefix length.
	[[nodiscard]] bool prepare(RENDER::Renderer& renderer, const Input& input);
	// The caller binds the shared RG32_INT/D32 framebuffer after drawing the HW prefix.
	void merge(RENDER::Renderer& renderer);
	[[nodiscard]] std::shared_ptr<RENDER::StorageBufferInterface> countBuffer() const;
	[[nodiscard]] uint64_t gpuBytes() const;

private:
	bool ensureShaders();
	bool ensureBuffers(RENDER::Renderer& renderer, uint32_t pixels);

	std::shared_ptr<RENDER::ShaderInterface> mClassify {};
	std::shared_ptr<RENDER::ShaderInterface> mRaster {};
	std::shared_ptr<RENDER::ShaderInterface> mMerge {};
	std::shared_ptr<RENDER::StorageBufferInterface> mProjected {};
	std::shared_ptr<RENDER::StorageBufferInterface> mCount {};
	std::shared_ptr<RENDER::StorageBufferInterface> mKeys {};
	std::shared_ptr<RENDER::UniformBufferInterface> mParameters {};
	uint32_t mPixelCapacity = 0;
	uint32_t mWidth = 0;
	uint32_t mHeight = 0;
	bool mShaderAttempted = false;
};
}
