#ifdef METAL_BACKEND
#include "textureMetal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#include "deviceMetal.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/stdLoader.h"

namespace IKIGAI::RENDER {

	MTLPixelFormat ToMetalPixelFormat(PixelFormat format) {
		switch (format) {
		case PixelFormat::R_FLOAT: return MTLPixelFormatR32Float;
		case PixelFormat::RG_FLOAT: return MTLPixelFormatRG32Float;
		case PixelFormat::RGB_FLOAT: return MTLPixelFormatRGBA32Float;
		case PixelFormat::RGBA_FLOAT: return MTLPixelFormatRGBA32Float;
		case PixelFormat::R_INT: return MTLPixelFormatR8Unorm;
		case PixelFormat::RG_INT: return MTLPixelFormatRG8Unorm;
		case PixelFormat::RGB_INT: return MTLPixelFormatRGBA8Unorm;
		case PixelFormat::RGBA_INT: return MTLPixelFormatRGBA8Unorm;
		case PixelFormat::R32_INT: return MTLPixelFormatR32Sint;
		case PixelFormat::RG32_INT: return MTLPixelFormatRG32Sint;
		case PixelFormat::RGB32_INT: return MTLPixelFormatRGBA32Sint;
		case PixelFormat::RGBA32_INT: return MTLPixelFormatRGBA32Sint;
		case PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT: return MTLPixelFormatDepth32Float_Stencil8;
		case PixelFormat::DEPTH32_FLOAT: return MTLPixelFormatDepth32Float;
		case PixelFormat::DEPTH32_FLOAT_S8X24_UINT: return MTLPixelFormatDepth32Float_Stencil8;
		case PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT: return MTLPixelFormatDepth32Float_Stencil8;
		case PixelFormat::BGRA_INT: return MTLPixelFormatBGRA8Unorm;
		}
		return MTLPixelFormatRGBA8Unorm;
	}

	MTLVertexFormat ToMetalVertexFormat(PixelFormat format) {
		switch (format) {
		case PixelFormat::R_FLOAT: return MTLVertexFormatFloat;
		case PixelFormat::RG_FLOAT: return MTLVertexFormatFloat2;
		case PixelFormat::RGB_FLOAT: return MTLVertexFormatFloat3;
		case PixelFormat::RGBA_FLOAT: return MTLVertexFormatFloat4;
		case PixelFormat::R32_INT: return MTLVertexFormatInt;
		case PixelFormat::RG32_INT: return MTLVertexFormatInt2;
		case PixelFormat::RGB32_INT: return MTLVertexFormatInt3;
		case PixelFormat::RGBA32_INT: return MTLVertexFormatInt4;
		default: return MTLVertexFormatFloat4;
		}
	}

	bool MetalFormatHasStencil(MTLPixelFormat format) {
		return format == MTLPixelFormatDepth32Float_Stencil8 || format == MTLPixelFormatDepth24Unorm_Stencil8;
	}

	namespace {
		uint32_t MipCount(uint32_t width, uint32_t height) {
			const uint32_t extent = std::max(width, height);
			if (extent == 0) {
				return 1;
			}
			return static_cast<uint32_t>(std::floor(std::log2(extent))) + 1;
		}

		NSUInteger BytesPerPixel(MTLPixelFormat format) {
			switch (format) {
			case MTLPixelFormatR8Unorm: return 1;
			case MTLPixelFormatRG8Unorm: return 2;
			case MTLPixelFormatRGBA8Unorm:
			case MTLPixelFormatBGRA8Unorm:
			case MTLPixelFormatR32Float:
			case MTLPixelFormatR32Sint: return 4;
			case MTLPixelFormatRG32Float:
			case MTLPixelFormatRG32Sint: return 8;
			case MTLPixelFormatRGBA32Float:
			case MTLPixelFormatRGBA32Sint: return 16;
			default: return 4;
			}
		}

		bool IsDepthFormat(PixelFormat format) {
			switch (format) {
			case PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT:
			case PixelFormat::DEPTH32_FLOAT:
			case PixelFormat::DEPTH32_FLOAT_S8X24_UINT:
			case PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT:
				return true;
			default:
				return false;
			}
		}

		MTLSamplerAddressMode ToAddress(WrapFilter wrap) {
			switch (wrap) {
			case WrapFilter::REPEAT: return MTLSamplerAddressModeRepeat;
			case WrapFilter::MIRRORED_REPEAT: return MTLSamplerAddressModeMirrorRepeat;
			case WrapFilter::MIRROR_CLAMP_TO_EDGE: return MTLSamplerAddressModeMirrorClampToEdge;
			case WrapFilter::CLAMP_TO_BORDER: return MTLSamplerAddressModeClampToZero;
			case WrapFilter::CLAMP_TO_EDGE: return MTLSamplerAddressModeClampToEdge;
			}
			return MTLSamplerAddressModeClampToEdge;
		}

		struct ImagePayload {
			std::vector<void*> pixels;
			bool freePixels = false;
		};

		void FreePayload(ImagePayload& payload, bool isFloat) {
			if (!payload.freePixels) {
				return;
			}
			for (void* pixel : payload.pixels) {
				if (!pixel) {
					continue;
				}
				if (isFloat) {
					UTILS::STBiImageFree(static_cast<float*>(pixel));
				} else {
					UTILS::STBiImageFree(static_cast<unsigned char*>(pixel));
				}
			}
			payload.pixels.clear();
		}

		void* DecodeBytes(const uint8_t* bytes, int length, bool isFloat, int& width, int& height, int& channels) {
			UTILS::STBiSetFlipVerticallyOnLoad(true);
			if (isFloat) {
				return UTILS::STBiLoadfFromMemory(bytes, length, &width, &height, &channels, 0);
			}
			auto* data = UTILS::STBiLoadFromMemory(bytes, length, &width, &height, &channels, 0);
			if (data && channels != 4) {
				UTILS::STBiImageFree(static_cast<unsigned char*>(data));
				data = UTILS::STBiLoadFromMemory(bytes, length, &width, &height, &channels, 4);
				channels = 4;
			}
			return data;
		}

		ImagePayload LoadPayload(TextureResource& descriptor, const std::vector<std::vector<uint8_t>>* fileData) {
			ImagePayload payload;
			if (descriptor.texType == TextureType::DEPTH || IsDepthFormat(descriptor.pixelType)) {
				if (descriptor.depth == 0) {
					descriptor.depth = 1;
				}
				descriptor.useMipmap = false;
				descriptor.mipMapCount = 1;
				return payload;
			}

			if (!descriptor.colorData.empty()) {
				payload.pixels.push_back(descriptor.colorData.data());
			} else if (fileData && !fileData->empty()) {
				for (const auto& file : *fileData) {
					int width = 0;
					int height = 0;
					int channels = 0;
					void* decoded = DecodeBytes(file.data(), static_cast<int>(file.size()), descriptor.isFloat, width, height, channels);
					if (decoded) {
						payload.pixels.push_back(decoded);
						descriptor.width = width;
						descriptor.height = height;
						descriptor.channels = channels;
					}
				}
				payload.freePixels = true;
			} else if (!descriptor.pathTexture.empty()) {
				auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
				for (const auto& path : descriptor.pathTexture) {
					auto file = fs.getFile(path);
					if (!file || !file->isOpened()) {
						LOG_ERROR << "Metal texture: failed to open " << path;
						continue;
					}
					const auto bytes = file->read();
					int width = 0;
					int height = 0;
					int channels = 0;
					void* decoded = DecodeBytes(bytes.data(), static_cast<int>(bytes.size()), descriptor.isFloat, width, height, channels);
					if (decoded) {
						payload.pixels.push_back(decoded);
						descriptor.width = width;
						descriptor.height = height;
						descriptor.channels = channels;
					}
				}
				payload.freePixels = true;
			}

			if (descriptor.depth == 0) {
				descriptor.depth = 1;
			}
			if (descriptor.useMipmap && descriptor.width > 0 && descriptor.height > 0) {
				descriptor.mipMapCount = static_cast<int>(MipCount(static_cast<uint32_t>(descriptor.width), static_cast<uint32_t>(descriptor.height)));
			} else {
				descriptor.mipMapCount = 1;
			}
			return payload;
		}

	}

	TextureMetal::TextureMetal(const TextureResource& descriptor, const std::vector<void*>& data) {
		init(descriptor, data);
	}

	TextureMetal::TextureMetal(uint32_t width, uint32_t height, MTLPixelFormat format, NSUInteger samples, bool depth) {
		mWidth = width;
		mHeight = height;
		mDepth = 1;
		mMetalFormat = format;
		mFormat = depth ? PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT : PixelFormat::BGRA_INT;
		mType = depth ? TextureType::DEPTH : TextureType::TEXTURE_2D;
		mMipCount = 1;

		id<MTLDevice> device = DeviceMetal::Get();
		if (!device || width == 0 || height == 0) {
			return;
		}
		MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
		desc.mipmapLevelCount = 1;
		desc.storageMode = MTLStorageModePrivate;
		desc.usage = MTLTextureUsageRenderTarget;
		if (samples == 1) {
			desc.usage |= MTLTextureUsageShaderRead;
		}
		if (samples > 1) {
			desc.textureType = MTLTextureType2DMultisample;
			desc.sampleCount = samples;
		}
		mTexture = [device newTextureWithDescriptor:desc];
		createSampler();
	}

	void TextureMetal::init(const TextureResource& descriptor, const std::vector<void*>& data) {
		mPath = !descriptor.path.empty() ? descriptor.path : (!descriptor.pathTexture.empty() ? descriptor.pathTexture.front() : std::string());
		mType = descriptor.texType;
		mFormat = descriptor.pixelType;
		mWidth = static_cast<size_t>(std::max(descriptor.width, 0));
		mHeight = static_cast<size_t>(std::max(descriptor.height, 0));
		mDepth = static_cast<size_t>(std::max(descriptor.depth, 1));
		mChannels = static_cast<size_t>(std::max(descriptor.channels, 0));
		mUseMipMap = descriptor.useMipmap && mType != TextureType::DEPTH;
		mMipCount = descriptor.mipMapCount > 0 ? static_cast<uint32_t>(descriptor.mipMapCount) : 1u;
		mMinFilter = descriptor.minFilter;
		mMagFilter = descriptor.magFilter;
		mWrapS = descriptor.wrapS;
		mWrapT = descriptor.wrapT;
		mWrapR = descriptor.wrapR;
		mMetalFormat = ToMetalPixelFormat(mFormat);
		mTexture = nil;
		createSampler();

		id<MTLDevice> device = DeviceMetal::Get();
		if (!device || mWidth == 0 || mHeight == 0) {
			if (mWidth == 0 || mHeight == 0) {
				LOG_ERROR << "Metal texture has zero size: " << (mPath.empty() ? std::string("<memory>") : mPath);
			}
			return;
		}

		const bool depth = mType == TextureType::DEPTH || IsDepthFormat(mFormat);
		const bool cube = mType == TextureType::TEXTURE_CUBE;
		const bool array = mType == TextureType::TEXTURE_2D_ARRAY;
		const bool volume = mType == TextureType::TEXTURE_3D;
		NSUInteger slices = 1;
		if (cube) {
			slices = 6;
		} else if (array) {
			slices = data.empty() ? static_cast<NSUInteger>(std::max<size_t>(mDepth, 1)) : data.size();
		}

		MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
		desc.pixelFormat = mMetalFormat;
		desc.width = mWidth;
		desc.height = mHeight;
		desc.mipmapLevelCount = depth ? 1 : std::max<NSUInteger>(mMipCount, 1);
		desc.storageMode = MTLStorageModePrivate;
		desc.sampleCount = 1;
		if (depth) {
			desc.textureType = MTLTextureType2D;
			desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
			desc.depth = 1;
		} else if (volume) {
			desc.textureType = MTLTextureType3D;
			desc.depth = std::max<NSUInteger>(mDepth, 1);
			desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;
		} else if (cube) {
			desc.textureType = MTLTextureTypeCube;
			desc.arrayLength = 1;
			desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;
		} else if (array) {
			desc.textureType = MTLTextureType2DArray;
			desc.arrayLength = slices;
			desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;
		} else {
			desc.textureType = MTLTextureType2D;
			desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;
		}
		mTexture = [device newTextureWithDescriptor:desc];
		if (!mTexture) {
			LOG_ERROR << "Metal texture allocation failed: " << mPath;
			return;
		}

		bool hasPixels = false;
		for (void* pixel : data) {
			if (pixel) {
				hasPixels = true;
				break;
			}
		}
		if (!hasPixels || depth) {
			return;
		}

		const NSUInteger bpp = BytesPerPixel(mMetalFormat);
		const NSUInteger alignment = 256;
		const NSUInteger tight = mWidth * bpp;
		const NSUInteger row = (tight + alignment - 1) & ~(alignment - 1);
		const NSUInteger imageBytes = row * mHeight;
		const NSUInteger sliceCount = volume ? 1 : slices;
		const NSUInteger bufferLength = volume ? row * mHeight * std::max<NSUInteger>(mDepth, 1) : imageBytes * sliceCount;
		id<MTLBuffer> staging = [device newBufferWithLength:bufferLength options:MTLResourceStorageModeShared];
		if (!staging) {
			return;
		}
		auto* destination = static_cast<uint8_t*>(staging.contents);
		std::memset(destination, 0, bufferLength);

		const int sourceChannels = descriptor.channels > 0 ? descriptor.channels : 4;
		const size_t sourceBpp = static_cast<size_t>(sourceChannels) * (descriptor.isFloat ? sizeof(float) : 1u);
		for (NSUInteger slice = 0; slice < (volume ? 1 : sliceCount); ++slice) {
			if (slice >= data.size() || !data[slice]) {
				continue;
			}
			const auto* source = static_cast<const uint8_t*>(data[slice]);
			uint8_t* sliceDst = destination + (volume ? 0 : slice * imageBytes);
			const NSUInteger rows = volume ? mHeight * std::max<NSUInteger>(mDepth, 1) : mHeight;
			if (sourceBpp == bpp) {
				for (NSUInteger y = 0; y < rows; ++y) {
					std::memcpy(sliceDst + y * row, source + y * tight, tight);
				}
			} else if (!descriptor.isFloat && sourceChannels == 3 && bpp == 4) {
				for (NSUInteger y = 0; y < rows; ++y) {
					const auto* srcRow = source + y * mWidth * 3;
					auto* dstRow = sliceDst + y * row;
					for (NSUInteger x = 0; x < mWidth; ++x) {
						dstRow[x * 4 + 0] = srcRow[x * 3 + 0];
						dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
						dstRow[x * 4 + 2] = srcRow[x * 3 + 2];
						dstRow[x * 4 + 3] = 255;
					}
				}
			}
		}

		id<MTLCommandQueue> queue = [device newCommandQueue];
		id<MTLCommandBuffer> command = [queue commandBuffer];
		id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
		if (volume) {
			[blit copyFromBuffer:staging
					sourceOffset:0
			   sourceBytesPerRow:row
			 sourceBytesPerImage:row * mHeight
					  sourceSize:MTLSizeMake(mWidth, mHeight, std::max<NSUInteger>(mDepth, 1))
					   toTexture:mTexture
				destinationSlice:0
				destinationLevel:0
			   destinationOrigin:MTLOriginMake(0, 0, 0)];
		} else {
			for (NSUInteger slice = 0; slice < sliceCount; ++slice) {
				[blit copyFromBuffer:staging
						sourceOffset:slice * imageBytes
				   sourceBytesPerRow:row
				 sourceBytesPerImage:imageBytes
						  sourceSize:MTLSizeMake(mWidth, mHeight, 1)
						   toTexture:mTexture
					destinationSlice:slice
					destinationLevel:0
				   destinationOrigin:MTLOriginMake(0, 0, 0)];
			}
		}
		if (!depth && mMipCount > 1) {
			[blit generateMipmapsForTexture:mTexture];
		}
		[blit endEncoding];
		[command commit];
		[command waitUntilCompleted];
	}

	void TextureMetal::createSampler() {
		id<MTLDevice> device = DeviceMetal::Get();
		if (!device) {
			return;
		}
		MTLSamplerDescriptor* sampler = [[MTLSamplerDescriptor alloc] init];
		sampler.minFilter = mMinFilter == MinMagFilter::NEAREST ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
		sampler.magFilter = mMagFilter == MinMagFilter::NEAREST ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
		sampler.mipFilter = mMipCount > 1
			? (mMinFilter == MinMagFilter::NEAREST ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear)
			: MTLSamplerMipFilterNotMipmapped;
		sampler.sAddressMode = ToAddress(mWrapS);
		sampler.tAddressMode = ToAddress(mWrapT);
		sampler.rAddressMode = ToAddress(mWrapR);
		mSampler = [device newSamplerStateWithDescriptor:sampler];
	}

	void TextureMetal::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
		auto& mutableDesc = const_cast<TextureResource&>(descriptor);
		ImagePayload payload = fileData.empty() ? LoadPayload(mutableDesc, nullptr) : LoadPayload(mutableDesc, &fileData);
		init(mutableDesc, payload.pixels);
		FreePayload(payload, mutableDesc.isFloat);
	}

	std::shared_ptr<TextureMetal> TextureMetal::Create(const std::string& path, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		TextureResource descriptor;
		descriptor.useMipmap = true;
		descriptor.path = path;
		descriptor.pathTexture.push_back(path);
		return Create(descriptor, allocator, deleter);
	}

	std::shared_ptr<TextureMetal> TextureMetal::Create(const TextureResource& descriptor, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		auto& mutableDesc = const_cast<TextureResource&>(descriptor);
		ImagePayload payload = LoadPayload(mutableDesc, nullptr);
		auto texture = AllocateTexture<TextureMetal>(allocator, deleter, mutableDesc, payload.pixels);
		FreePayload(payload, mutableDesc.isFloat);
		return texture;
	}

	std::shared_ptr<TextureMetal> TextureMetal::Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		auto& mutableDesc = const_cast<TextureResource&>(descriptor);
		ImagePayload payload = fileData.empty() ? LoadPayload(mutableDesc, nullptr) : LoadPayload(mutableDesc, &fileData);
		auto texture = AllocateTexture<TextureMetal>(allocator, deleter, mutableDesc, payload.pixels);
		FreePayload(payload, mutableDesc.isFloat);
		return texture;
	}

	TextureAtlasMetal::TextureAtlasMetal(const TextureResource& descriptor, const std::vector<void*>& data)
		: TextureMetal(descriptor, data) {
		loadAtlas(descriptor);
	}

	void TextureAtlasMetal::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
		TextureMetal::recreate(descriptor, fileData);
		loadAtlas(descriptor);
	}

	void TextureAtlasMetal::loadAtlas(const TextureResource& descriptor) {
		if (descriptor.pathTexture.empty()) {
			return;
		}
		std::filesystem::path configPath{descriptor.pathTexture.front()};
		configPath.replace_extension(".atlas");
		auto atlas = UTILS::FromJson<AtlasData>(configPath.string());
		if (atlas.isOk()) {
			mAtlas = atlas.unwrap();
		}
	}

	AtlasRect TextureAtlasMetal::getPiece(const std::string& name) const {
		if (mAtlas.mRects.contains(name)) {
			return mAtlas.mRects.at(name);
		}
		return {};
	}

	AtlasRect TextureAtlasMetal::getPieceUV(const std::string& name) const {
		if (!mAtlas.mRects.contains(name) || mWidth == 0 || mHeight == 0) {
			return {};
		}
		auto rect = mAtlas.mRects.at(name);
		rect.mX /= static_cast<float>(mWidth);
		rect.mY /= static_cast<float>(mHeight);
		rect.mW /= static_cast<float>(mWidth);
		rect.mH /= static_cast<float>(mHeight);
		return rect;
	}

	std::shared_ptr<TextureAtlasMetal> TextureAtlasMetal::CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		TextureResource descriptor;
		descriptor.useMipmap = generateMipmap;
		descriptor.path = path;
		descriptor.pathTexture.push_back(path);
		return CreateAtlasFromResource(descriptor, {}, allocator, deleter);
	}

	std::shared_ptr<TextureAtlasMetal> TextureAtlasMetal::CreateAtlasFromResource(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
		auto& mutableDesc = const_cast<TextureResource&>(descriptor);
		ImagePayload payload = fileData.empty() ? LoadPayload(mutableDesc, nullptr) : LoadPayload(mutableDesc, &fileData);
		auto texture = AllocateTexture<TextureAtlasMetal>(allocator, deleter, mutableDesc, payload.pixels);
		FreePayload(payload, mutableDesc.isFloat);
		return texture;
	}

}
#endif
