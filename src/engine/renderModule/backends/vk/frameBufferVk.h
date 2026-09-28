#pragma once

#ifdef VULKAN_BACKEND
#include "renderModule/backends/interface/frameBufferInterface.h"
#include <memory>
#include <vector>
#include <volk.h>
namespace IKIGAI::RENDER {
	class FrameBufferVk : public FrameBufferInterface {
	private:
		std::vector<std::shared_ptr<TextureInterface>> mTextures;
		std::shared_ptr<TextureInterface> mDepth;

	public:
		FrameBufferVk(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth = nullptr);
		const std::vector<std::shared_ptr<TextureInterface>>& getTextures() const override;
		const std::shared_ptr<TextureInterface>& getDepth() const override;
	};
}
#endif

