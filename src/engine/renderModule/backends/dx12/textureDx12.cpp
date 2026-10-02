#include "textureDx12.h"

#include "utilsModule/pathGetter.h"

#ifdef DX12_BACKEND
#include "d3dx12/d3d12generatemips.h"
#include "d3dx12/DirectXHelpers.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderModule/backends/interface/atlasInterface.h"
#include "utilsModule/stdLoader.h"
#include "utilsModule/jsonLoader.h"
#include "d3dUtil.h"
#include <stdexcept>
#include <filesystem>

#include "driverDx12.h"
#include <resourceModule/serviceManager.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include "utilsModule/log/loggerDefine.h"

#define STB_IMAGE_IMPLEMENTATION
#include <functional>
#include <coreModule/glmWrapper.hpp>


using namespace IKIGAI;
using namespace IKIGAI::RENDER;

static uint32_t GetMipCount(uint32_t width, uint32_t height) {
	return static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(glm::max(width, height))))) + 1;
}

static bool LoadTextureImageFromVfs(const std::string& path, bool isFloat, int& width, int& height, int& channels, void*& outData) {
	auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(path, RESOURCES::FileMode::READ);
	if (!file) {
		LOG_ERROR << "Failed to open texture: " << path;
		return false;
	}
	const auto fileData = file->read();
	if (isFloat) {
		outData = UTILS::STBiLoadfFromMemory(fileData.data(), fileData.size(), &width, &height, &channels, 0);
	} else {
		auto* data = UTILS::STBiLoadFromMemory(fileData.data(), fileData.size(), &width, &height, &channels, 0);
		if (data && channels == 3) {
			UTILS::STBiImageFree((unsigned char*)data);
			data = UTILS::STBiLoadFromMemory(fileData.data(), fileData.size(), &width, &height, &channels, 4);
			channels = 4;
		}
		outData = data;
	}
	if (!outData) {
		LOG_ERROR << "Failed to decode texture: " << path;
		return false;
	}
	return true;
}

static const std::map<PixelFormat, DXGI_FORMAT> FormatMap = {
	{ PixelFormat::R_FLOAT, DXGI_FORMAT_R32_FLOAT },
	{ PixelFormat::RG_FLOAT, DXGI_FORMAT_R32G32_FLOAT },
	{ PixelFormat::RGB_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT },
	{ PixelFormat::RGBA_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT },
	{ PixelFormat::R_INT, DXGI_FORMAT_R8_UNORM },
	{ PixelFormat::RG_INT, DXGI_FORMAT_R8G8_UNORM },
	{ PixelFormat::RGB_INT, DXGI_FORMAT_R8G8B8A8_UNORM },
	{ PixelFormat::RGB_INT, DXGI_FORMAT_R8G8B8A8_UNORM },
	{ PixelFormat::RGBA_INT, DXGI_FORMAT_R8G8B8A8_UNORM },
	{ PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, DXGI_FORMAT_D24_UNORM_S8_UINT },
	{ PixelFormat::DEPTH32_FLOAT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT},
	{ PixelFormat::DEPTH32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT },
};

uint32_t GetFormatChannelsCount(PixelFormat format) {
	static const std::map<PixelFormat, uint32_t> FormatChannelsMap = {
		{PixelFormat::R_FLOAT, 1},
		{PixelFormat::RG_FLOAT, 2},
		{PixelFormat::RGB_FLOAT, 3},
		{PixelFormat::RGBA_FLOAT, 4},
		{PixelFormat::R_INT, 1},
		{PixelFormat::RG_INT, 2},
		{PixelFormat::RGB_INT, 3},
		{PixelFormat::RGBA_INT, 4},
		{PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, 1},
		{PixelFormat::DEPTH32_FLOAT, 1},
		{PixelFormat::DEPTH32_FLOAT_S8X24_UINT, 1},
	};
	return FormatChannelsMap.at(format);
}

uint32_t GetFormatChannelSize(PixelFormat format) {
	static const std::map<PixelFormat, uint32_t> FormatChannelSizeMap = {
		{PixelFormat::R_FLOAT, 4},
		{PixelFormat::RG_FLOAT, 4},
		{PixelFormat::RGB_FLOAT, 4},
		{PixelFormat::RGBA_FLOAT, 4},
		{PixelFormat::R_INT, 1},
		{PixelFormat::RG_INT, 1},
		{PixelFormat::RGB_INT, 1},
		{PixelFormat::RGBA_INT, 1},
		{PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, 4},
		{PixelFormat::DEPTH32_FLOAT, 4},
		{PixelFormat::DEPTH32_FLOAT_S8X24_UINT, 8},
	};
	return FormatChannelSizeMap.at(format);
}

const Dx12Resource& TextureDx12::getResource() const {
	return mResource;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE TextureDx12::getGpuDescriptorHandle() const {
	return mGpuDescriptorHandle;
}

TextureDx12::TextureDx12(const TextureResource& descriptor, const std::vector<void*>& data) {
	create(descriptor, data);
}

TextureDx12::TextureDx12(size_t width, size_t height, PixelFormat format, Microsoft::WRL::ComPtr<ID3D12Resource> texture) :
	mResource(texture) {
	mWidth = width;
	mHeight = height;
	mFormat = format;
	mChannels = GetFormatChannelsCount(format);
}

TextureDx12::~TextureDx12() {
	d3dUtil::GetDriver()->destroyDeferred(mResource);
}

void TextureDx12::create(const TextureResource& descriptor, const std::vector<void*>& data) {
	mType = descriptor.texType;
	mFormat = descriptor.pixelType;
	mWidth = descriptor.width;
	mHeight = descriptor.height;
	mDepth = descriptor.depth != 0 ? descriptor.depth : 1;
	mChannels = descriptor.channels != 0 ? descriptor.channels : GetFormatChannelsCount(mFormat);
	mMipCount = descriptor.mipMapCount != 0 ? descriptor.mipMapCount : 1;
	if (mWidth == 0 || mHeight == 0) {
		throw std::runtime_error("TextureDx12::create: invalid texture dimensions");
	}
	if (descriptor.texType == TextureType::DEPTH) {
		mMipCount = 1;
	}
	mMinFilter = descriptor.minFilter;
	mMagFilter = descriptor.magFilter;

	mWrapS = descriptor.wrapS;
	mWrapT = descriptor.wrapT;
	mWrapR = descriptor.wrapR;

	const size_t arrSize = data.empty() ? 1 : data.size();
	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(FormatMap.at(mFormat), mWidth, mHeight, arrSize, mMipCount);
	D3D12MA::ALLOCATION_FLAGS allocFlags = D3D12MA::ALLOCATION_FLAG_NONE;
	if (descriptor.texType == TextureType::DEPTH) {
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		allocFlags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
	}
	else {
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		allocFlags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
	}
	
	mCurrentState = D3D12_RESOURCE_STATE_COMMON;
	mResource = d3dUtil::CreateResource(desc, D3D12_HEAP_TYPE_DEFAULT, mCurrentState, nullptr, allocFlags);

	if (descriptor.texType != TextureType::DEPTH) {
		DirectX::CreateShaderResourceView(d3dUtil::GetDriver()->getDevice().Get(), mResource.Get(), d3dUtil::GetDriver()->getDescriptorHeapCPUHandle(), mType == TextureType::TEXTURE_CUBE);
	}
	if (descriptor.texType == TextureType::DEPTH) {
		d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdList) {
			setState(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		});
	}

	mGpuDescriptorHandle = d3dUtil::GetDriver()->getDescriptorHeapGPUHandle();
	d3dUtil::GetDriver()->getDescriptorHeapCPUHandle().Offset(1, d3dUtil::GetDriver()->getDescriptorIncSize());
	d3dUtil::GetDriver()->getDescriptorHeapGPUHandle().Offset(1, d3dUtil::GetDriver()->getDescriptorIncSize());


	if (data.size()) {
		setData(data, mWidth, mHeight, mFormat, 0);
	}
	if (descriptor.useMipmap) {
		generateMips();
	}
}

void TextureDx12::setData(const std::vector<void*>& data, size_t width, size_t height, PixelFormat format, size_t mipLevel) {
	const auto uploadSize = GetRequiredIntermediateSize(mResource.Get(), mipLevel, 1);
	auto buffer = d3dUtil::CreateBuffer(uploadSize, D3D12_HEAP_TYPE_UPLOAD);

	const auto channels = GetFormatChannelsCount(format);
	const auto channelSize = GetFormatChannelSize(format);

	std::vector<D3D12_SUBRESOURCE_DATA> subersources;
	for (const auto* d : data) {
		D3D12_SUBRESOURCE_DATA subersource{};
		subersource.pData = d;
		subersource.RowPitch = width * channels * channelSize;
		subersource.SlicePitch = width * height * channels * channelSize;
		subersources.push_back(subersource);
	}
	d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdList) {
		setState(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
		UpdateSubresources(cmdList, mResource.Get(), buffer.Get(), 0, mipLevel, subersources.size(), subersources.data());
	});
}

void TextureDx12::generateMips(ID3D12GraphicsCommandList* cmdList) {
	setState(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	D3D12GenerateMips(d3dUtil::GetDriver()->getDevice().Get(), cmdList, mResource.Get(), d3dUtil::GetDriver()->getDestroyDeferredObjects());
	mCurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void TextureDx12::generateMips() {
	d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdList) {
		generateMips(cmdList);
	});
}

void TextureDx12::setState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state) {
	if (mCurrentState == state) {
		return;
	}
	DirectX::TransitionResource(cmdList, mResource.Get(), mCurrentState, state);
	mCurrentState = state;
}

void* TextureDx12::getImguiId() {
	return (void*)mGpuDescriptorHandle.ptr;
}

void TextureDx12::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
	// Отложенное уничтожение старого DX12-ресурса
	d3dUtil::GetDriver()->destroyDeferred(mResource);
	mResource = nullptr;

	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	bool needFree = false;

	if (_d.colorData.empty() && !fileData.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		for (const auto& fData : fileData) {
			int w = 0, h = 0, c = 0;
			if (_d.isFloat) {
				textureData.push_back(IKIGAI::UTILS::STBiLoadfFromMemory(fData.data(), fData.size(), &w, &h, &c, 0));
			} else {
				auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 0);
				if (c == 3) {
					IKIGAI::UTILS::STBiImageFree((unsigned char*)data);
					data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 4);
				}
				textureData.push_back(data);
			}
			_d.width = w; _d.height = h; _d.channels = c;
		}
		needFree = true;
	} else if (!_d.colorData.empty()) {
		textureData.push_back(const_cast<uint8_t*>(_d.colorData.data()));
	}
	if (_d.useMipmap) _d.mipMapCount = GetMipCount(_d.width, _d.height);

	create(descriptor, textureData);

	if (needFree) {
		for (auto* ptr : textureData) {
			if (descriptor.isFloat) IKIGAI::UTILS::STBiImageFree((float*)ptr);
			else IKIGAI::UTILS::STBiImageFree((unsigned char*)ptr);
		}
	}
}

std::shared_ptr<TextureDx12> TextureDx12::Create(const std::string& path, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	TextureResource res;
	res.useMipmap = true;
	res.pathTexture.push_back(path);
	return Create(res, allocator, deleter);
}

std::shared_ptr<TextureDx12> TextureDx12::Create(const TextureResource& descriptor, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	auto& _descriptor = const_cast<TextureResource&>(descriptor);
	//Load data
	std::vector<void*> textureData;
	if (!_descriptor.pathTexture.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		for (const auto& path : _descriptor.pathTexture) {
			int width = 0, height = 0, channels = 0;
			void* data = nullptr;
			if (!LoadTextureImageFromVfs(path, _descriptor.isFloat, width, height, channels, data)) {
				continue;
			}
			textureData.push_back(data);
			_descriptor.width = width;
			_descriptor.height = height;
			_descriptor.channels = channels;
		}
	} else if (!_descriptor.colorData.empty()) {
		textureData.push_back((void*)_descriptor.colorData.data());
	}

	if (_descriptor.depth == 0) {
		_descriptor.depth = 1;
	}
	if (_descriptor.useMipmap) {
		_descriptor.mipMapCount = GetMipCount(_descriptor.width, _descriptor.height);
	} else {
		_descriptor.mipMapCount = 1;
	}

	if (textureData.empty() && !_descriptor.pathTexture.empty()) {
		LOG_ERROR << "TextureDx12::Create: no pixel data loaded";
		return nullptr;
	}

	//Create texture
	auto tex = AllocateTexture<TextureDx12>(allocator, deleter, descriptor, textureData);

	//Free texture data
	if (!_descriptor.pathTexture.empty()) {
		for (auto data : textureData) {
			if (_descriptor.isFloat) {
				UTILS::STBiImageFree((float*)data);
			} else {
				UTILS::STBiImageFree((unsigned char*)data);
			}
		}
	}

	return tex;
}

std::shared_ptr<TextureDx12> TextureDx12::Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	bool needFree = false;

	if (_d.colorData.empty() && !fileData.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		for (const auto& fData : fileData) {
			int w = 0, h = 0, c = 0;
			if (_d.isFloat) {
				textureData.push_back(IKIGAI::UTILS::STBiLoadfFromMemory(fData.data(), fData.size(), &w, &h, &c, 0));
			} else {
				auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 0);
				if (c == 3) {
					IKIGAI::UTILS::STBiImageFree((unsigned char*)data);
					data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 4);
					c = 4;
				}
				textureData.push_back(data);
			}
			_d.width = w; _d.height = h; _d.channels = c;
		}
		needFree = true;
	} else if (!_d.colorData.empty()) {
		textureData.push_back((void*)_d.colorData.data());
	}

	if (_d.depth == 0) _d.depth = 1;

	auto tex = AllocateTexture<TextureDx12>(allocator, deleter, descriptor, textureData);

	if (needFree) {
		for (auto* ptr : textureData) {
			if (descriptor.isFloat) IKIGAI::UTILS::STBiImageFree((float*)ptr);
			else IKIGAI::UTILS::STBiImageFree((unsigned char*)ptr);
		}
	}
	return tex;
}

AtlasRect TextureAtlasDx12::getPiece(const std::string& name) const {
	if (mAtlas.mRects.contains(name)) {
		return mAtlas.mRects.at(name);
	}
	return AtlasRect{};
}

AtlasRect TextureAtlasDx12::getPieceUV(const std::string& name) const {
	if (mAtlas.mRects.contains(name)) {
		auto res = mAtlas.mRects.at(name);
		res.mX /= mWidth;
		res.mY /= mHeight;
		res.mW /= mWidth;
		res.mH /= mHeight;
		return res;
	}
	return AtlasRect{};
}

void TextureAtlasDx12::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
	TextureDx12::recreate(descriptor, fileData);
}

std::shared_ptr<TextureAtlasDx12> TextureAtlasDx12::CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	TextureResource res;
	res.useMipmap = generateMipmap;
	res.pathTexture.push_back(path);
	return CreateAtlasFromResource(res, allocator, deleter);
}

std::shared_ptr<TextureAtlasDx12> TextureAtlasDx12::CreateAtlasFromResource(const TextureResource& descriptor, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	if (!_d.pathTexture.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		const auto& path = _d.pathTexture[0];
		int w = 0, h = 0, c = 0;
		void* data = nullptr;
		if (LoadTextureImageFromVfs(path, _d.isFloat, w, h, c, data)) {
			textureData.push_back(data);
			_d.width = w;
			_d.height = h;
			_d.channels = c;
		}
	} else if (!_d.colorData.empty()) {
		textureData.push_back((void*)_d.colorData.data());
	}
	if (_d.depth == 0) {
		_d.depth = 1;
	}
	if (_d.useMipmap) {
		_d.mipMapCount = GetMipCount(_d.width, _d.height);
	} else {
		_d.mipMapCount = 1;
	}

	if (textureData.empty() && !_d.pathTexture.empty()) {
		LOG_ERROR << "TextureAtlasDx12::CreateAtlasFromResource: no pixel data loaded";
		return nullptr;
	}

	auto tex = AllocateTexture<TextureAtlasDx12>(allocator, deleter, descriptor, textureData);

	if (!_d.pathTexture.empty() && !textureData.empty()) {
		if (_d.isFloat) {
			UTILS::STBiImageFree((float*)textureData[0]);
		} else {
			IKIGAI::UTILS::STBiImageFree((unsigned char*)textureData[0]);
		}
	}

	if (!_d.pathTexture.empty()) {
		std::filesystem::path configPath{ _d.pathTexture[0] };
		configPath.replace_extension(".atlas");
		auto atlasRes = IKIGAI::UTILS::FromJson<IKIGAI::RENDER::AtlasData>(configPath.string());
		if (atlasRes.isOk()) {
			tex->mAtlas = atlasRes.unwrap();
		}
	}
	return tex;
}

std::shared_ptr<TextureAtlasDx12> TextureAtlasDx12::CreateAtlasFromResource(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	bool needFree = false;

	if (_d.colorData.empty() && !fileData.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		const auto& fData = fileData[0];
		int w = 0, h = 0, c = 0;
		auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 0);
		if (c == 3) {
			IKIGAI::UTILS::STBiImageFree((unsigned char*)data);
			data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 4); c = 4;
		}
		textureData.push_back(data);
		_d.width = w; _d.height = h; _d.channels = c;
		needFree = true;
	} else if (!_d.colorData.empty()) {
		textureData.push_back((void*)_d.colorData.data());
	}
	if (_d.depth == 0) {
		_d.depth = 1;
	}
	if (_d.useMipmap) {
		_d.mipMapCount = GetMipCount(_d.width, _d.height);
	}

	auto tex = AllocateTexture<TextureAtlasDx12>(allocator, deleter, descriptor, textureData);

	if (needFree && !textureData.empty()) {
		IKIGAI::UTILS::STBiImageFree((unsigned char*)textureData[0]);
	}

	if (!_d.pathTexture.empty()) {
		std::filesystem::path configPath{ _d.pathTexture[0] };
		configPath.replace_extension(".atlas");
		auto atlasRes = IKIGAI::UTILS::FromJson<IKIGAI::RENDER::AtlasData>(configPath.string());
		if (atlasRes.isOk()) {
			tex->mAtlas = atlasRes.unwrap();
		}
	}
	return tex;
}

#endif
