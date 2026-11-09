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
#include "utilsModule/memory/memory.h"
#include "vmaVk.h"

namespace IKIGAI::RENDER {
	class TextureVk : public TextureInterface {
	public:
		VmaImage mImage;
		vk::Image mImagePtr;
		vk::raii::ImageView mImageView = nullptr;
		size_t mDepth = 0;
		bool mDiscardAliasContents = false;

		vk::raii::Sampler mSampler = nullptr;

		VkDescriptorSet mDescriptorSet = nullptr;
		
		TextureVk() = default;
		TextureVk(const TextureResource& descriptor, const std::vector<void*>& data, std::shared_ptr<VmaImage>* backing = nullptr);
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
		vk::ImageView attachmentView(unsigned mip, unsigned layer);
		std::map<std::pair<unsigned, unsigned>, vk::raii::ImageView> mAttachmentViews;
		vk::ImageView baseMipStorageView();
		void setState(const vk::raii::CommandBuffer& cmdbuf, vk::ImageLayout state);
		void setState(const vk::raii::CommandBuffer& cmdbuf, vk::ImageLayout state, uint32_t mip, uint32_t layer);
		void applyImageBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::ImageMemoryBarrier2 barrier);
		[[nodiscard]] bool isInState(vk::ImageLayout state) const;
		[[nodiscard]] vk::ImageUsageFlags imageUsage() const {
			return mImageUsage;
		}
		[[nodiscard]] vk::ImageLayout state(uint32_t mip, uint32_t layer) const;
		[[nodiscard]] uint32_t arrayLayerCount() const;
		[[nodiscard]] vk::ImageSubresourceRange subresourceRange(uint32_t mip, uint32_t layer) const;
		void resetAliasedState(const vk::raii::CommandBuffer& cmdbuf);

	protected:
		void releaseResources();
		// Загрузить данные и инициализировать VK-ресурсы (выделено из конструктора)
		void init(const TextureResource& descriptor, const std::vector<void*>& data, std::shared_ptr<VmaImage>* backing = nullptr);

	private:
		[[nodiscard]] vk::ImageAspectFlags layoutAspects() const;
		[[nodiscard]] size_t layoutIndex(uint32_t mip, uint32_t layer) const;
		vk::ImageViewType mViewType = vk::ImageViewType::e2D;
		vk::ImageUsageFlags mImageUsage {};
		vk::raii::ImageView mBaseMipStorageView = nullptr;
		std::vector<vk::ImageLayout> mLayouts { vk::ImageLayout::eUndefined };
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
