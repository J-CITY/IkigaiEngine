#pragma once
#include "atlasInterface.h"
#include "renderEnums.h"
#include "renderResourceOwner.h"
#include <string>

namespace IKIGAI::RENDER {
	struct TextureResource; // forward declaration (defined in resourceStruct.h)

	enum class TextureType {
		TEXTURE_2D = 0,
		TEXTURE_3D,
		TEXTURE_CUBE,
		TEXTURE_2D_ARRAY,
		DEPTH
	};

	class TextureInterface : protected RenderResourceOwner {
	protected:
		std::string mPath;

		TextureType mType = TextureType::TEXTURE_2D;
		PixelFormat mFormat = PixelFormat::RGBA_INT;

		size_t mWidth = 0;
		size_t mHeight = 0;
		size_t mDepth = 1;
		size_t mChannels = 1;

		uint32_t mMipCount = 0;
        uint64_t mRevision = 1;

		MinMagFilter mMinFilter = MinMagFilter::LINEAR;
		MinMagFilter mMagFilter = MinMagFilter::LINEAR;

		WrapFilter mWrapS = WrapFilter::CLAMP_TO_EDGE;
		WrapFilter mWrapT = WrapFilter::CLAMP_TO_EDGE;
		WrapFilter mWrapR = WrapFilter::CLAMP_TO_EDGE;

		bool mUseMipMap = false;

	public:
		TextureType getType() const { return mType; }
		PixelFormat getFormat() const { return mFormat; }
		const std::string& getPath() const { return mPath; }
		size_t getWidth() const { return mWidth; }
		size_t getHeight() const { return mHeight; }
		size_t getDepth() const { return mDepth; }
		size_t getChannels() const { return mChannels; }
		uint32_t getMipCount() const { return mMipCount; }
        uint64_t getRevision() const { return mRevision; }
		bool getUseMipMap() const { return mUseMipMap; }
        MinMagFilter getMinFilter() const {return mMinFilter;}
        MinMagFilter getMagFilter() const {return mMagFilter;}
        WrapFilter getWrapS() const {return mWrapS;}
        WrapFilter getWrapT() const {return mWrapT;}
        WrapFilter getWrapR() const {return mWrapR;}
		virtual AtlasRect getPieceUV(const std::string&) const { return {0,0,1,1}; }
		virtual AtlasRect getPiece(const std::string&) const { return {0,0,static_cast<float>(mWidth),static_cast<float>(mHeight)}; }

		virtual ~TextureInterface() = default;
		virtual void* getImguiId() = 0;
		// Пересоздать GPU-ресурс из нового/обновлённого дескриптора (для hot-reload)
		// fileData - это сырые байты файлов (PNG/JPG), загруженные менеджером (один или несколько)
		virtual void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) = 0;
	};
}
