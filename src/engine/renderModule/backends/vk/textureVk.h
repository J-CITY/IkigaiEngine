#pragma once
#include "renderModule/backends/interface/resourceStruct.h"
#ifdef VULKAN_BACKEND
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include "../interface/textureInterface.h"
#include "../interface/driverInterface.h"
#include "../interface/atlasInterface.h"
#include "utilsModule/memoryAlloc.h"

namespace IKIGAI::RENDER {
	class TextureVk : public TextureInterface {
	public:
		vk::raii::Image mImage = nullptr;
		vk::raii::DeviceMemory mDeviceMemory = nullptr;
		vk::Image mImagePtr;
		vk::raii::ImageView mImageView = nullptr;
		size_t mDepth = 0;
		vk::ImageLayout mCurrentState = vk::ImageLayout::eUndefined;

		vk::raii::Sampler mSampler = nullptr;

		VkDescriptorSet mDescriptorSet = nullptr;
		
		TextureVk() = default;
		TextureVk(const TextureResource& descriptor, const std::vector<void*>& data);
		TextureVk(uint32_t width, uint32_t height, vk::Format format, vk::Image image);
		~TextureVk() override;

		void* getImguiId() override;
		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		static std::shared_ptr<TextureVk> Create(const std::string& path, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureVk> Create(const TextureResource& descriptor, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureVk> Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		void setData(uint32_t width, uint32_t height, PixelFormat format, const std::vector<void*>& data, uint32_t mip_level,
		             uint32_t offset_x, uint32_t offset_y);
		void generateMips();
		void setState(const vk::raii::CommandBuffer& cmdbuf, vk::ImageLayout state);

	protected:
		// Загрузить данные и инициализировать VK-ресурсы (выделено из конструктора)
		void init(const TextureResource& descriptor, const std::vector<void*>& data);
	};

	class TextureAtlasVk : public TextureVk {
		AtlasData mAtlas;
	public:
		TextureAtlasVk() = default;
		TextureAtlasVk(const TextureResource& descriptor, const std::vector<void*>& data)
			: TextureVk(descriptor, data) {}

		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		[[nodiscard]] AtlasRect getPiece(const std::string& name) const;
		[[nodiscard]] AtlasRect getPieceUV(const std::string& name) const;

		static std::shared_ptr<TextureAtlasVk> CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasVk> CreateAtlasFromResource(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasVk> CreateAtlasFromResource(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
	};
}
#endif
