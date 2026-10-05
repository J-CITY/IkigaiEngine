#pragma once
#ifdef METAL_BACKEND

#include "../interface/atlasInterface.h"
#include "../interface/driverInterface.h"
#include "../interface/textureInterface.h"
#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

	MTLPixelFormat ToMetalPixelFormat(PixelFormat format);
	MTLVertexFormat ToMetalVertexFormat(PixelFormat format);
	bool MetalFormatHasStencil(MTLPixelFormat format);

	class TextureMetal : public TextureInterface {
	public:
		TextureMetal(const TextureResource& descriptor, const std::vector<void*>& data);
		TextureMetal(uint32_t width, uint32_t height, MTLPixelFormat format, NSUInteger samples, bool depth);
		~TextureMetal() override = default;

		void* getImguiId() override { return (__bridge void*)mTexture; }
		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		id<MTLTexture> getTexture() const { return mTexture; }
		id<MTLSamplerState> getSampler() const { return mSampler; }
		MTLPixelFormat getMetalFormat() const { return mMetalFormat; }

		static std::shared_ptr<TextureMetal> Create(const std::string& path, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureMetal> Create(const TextureResource& descriptor, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureMetal> Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);

	protected:
		void init(const TextureResource& descriptor, const std::vector<void*>& data);
		void createSampler();

		id<MTLTexture> mTexture = nil;
		id<MTLSamplerState> mSampler = nil;
		MTLPixelFormat mMetalFormat = MTLPixelFormatRGBA8Unorm;
	};

	class TextureAtlasMetal : public TextureMetal {
	public:
		TextureAtlasMetal(const TextureResource& descriptor, const std::vector<void*>& data);

		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		[[nodiscard]] AtlasRect getPiece(const std::string& name) const;
		[[nodiscard]] AtlasRect getPieceUV(const std::string& name) const;

		static std::shared_ptr<TextureAtlasMetal> CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasMetal> CreateAtlasFromResource(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);

	private:
		void loadAtlas(const TextureResource& descriptor);
		AtlasData mAtlas;
	};

}

#endif
