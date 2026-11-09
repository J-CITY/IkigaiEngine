#include "textureVk.h"


#ifdef VULKAN_BACKEND
#include <iostream>
#include <filesystem>

#include "utilsModule/stdLoader.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "driverVk.h"

#include <resourceModule/textureManager.h>

#include <resourceModule/serviceManager.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <renderModule/backends/interface/atlasInterface.h>
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"

#include "renderModule/backends/interface/integerColorAttachment.h"

#include <cmath>
#include <algorithm>
static uint32_t GetMipCount(uint32_t width, uint32_t height) {
	return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

static const std::unordered_map<IKIGAI::RENDER::PixelFormat, vk::Format> FormatMap = {
	{IKIGAI::RENDER::PixelFormat::R_FLOAT, vk::Format::eR32Sfloat},
	{IKIGAI::RENDER::PixelFormat::RG_FLOAT, vk::Format::eR32G32Sfloat},
	{ IKIGAI::RENDER::PixelFormat::RG32_INT, vk::Format::eR32G32Sint },
	{IKIGAI::RENDER::PixelFormat::RGB_FLOAT, vk::Format::eR32G32B32Sfloat},
	{IKIGAI::RENDER::PixelFormat::RGBA_FLOAT, vk::Format::eR32G32B32A32Sfloat},
	{IKIGAI::RENDER::PixelFormat::R_INT, vk::Format::eR8Unorm},
	{IKIGAI::RENDER::PixelFormat::RG_INT, vk::Format::eR8G8Unorm},
	{IKIGAI::RENDER::PixelFormat::RGB_INT, vk::Format::eR8G8B8Unorm},
	{IKIGAI::RENDER::PixelFormat::RGBA_INT, vk::Format::eR8G8B8A8Unorm},
	{IKIGAI::RENDER::PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT, vk::Format::eD24UnormS8Uint},
	{IKIGAI::RENDER::PixelFormat::DEPTH32_FLOAT, vk::Format::eD32Sfloat},
	{IKIGAI::RENDER::PixelFormat::DEPTH32_FLOAT_S8X24_UINT, vk::Format::eX8D24UnormPack32},
	{IKIGAI::RENDER::PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT, vk::Format::eD32SfloatS8Uint},
	{ IKIGAI::RENDER::PixelFormat::RGBA16_FLOAT, vk::Format::eR16G16B16A16Sfloat },
	{ IKIGAI::RENDER::PixelFormat::RG16_FLOAT, vk::Format::eR16G16Sfloat },
	{ IKIGAI::RENDER::PixelFormat::R16_FLOAT, vk::Format::eR16Sfloat },
	{ IKIGAI::RENDER::PixelFormat::R11G11B10_FLOAT, vk::Format::eB10G11R11UfloatPack32 },
};

static const std::unordered_map<vk::Format, IKIGAI::RENDER::PixelFormat> FormatMap2 = {
	{vk::Format::eR32Sfloat, IKIGAI::RENDER::PixelFormat::R_FLOAT},
	{vk::Format::eR32G32Sfloat, IKIGAI::RENDER::PixelFormat::RG_FLOAT},
	{ vk::Format::eR32G32Sint, IKIGAI::RENDER::PixelFormat::RG32_INT },
	{vk::Format::eR32G32B32Sfloat, IKIGAI::RENDER::PixelFormat::RGB_FLOAT},
	{vk::Format::eR32G32B32A32Sfloat, IKIGAI::RENDER::PixelFormat::RGBA_FLOAT},
	{vk::Format::eR8Unorm, IKIGAI::RENDER::PixelFormat::R_INT},
	{vk::Format::eR8G8Unorm, IKIGAI::RENDER::PixelFormat::RG_INT},
	{vk::Format::eR8G8B8Unorm, IKIGAI::RENDER::PixelFormat::RGB_INT},
	{vk::Format::eR8G8B8A8Unorm, IKIGAI::RENDER::PixelFormat::RGBA_INT},
	{vk::Format::eD24UnormS8Uint, IKIGAI::RENDER::PixelFormat::DEPTH_24_UNORM_STENCIL_8_UINT},
	{vk::Format::eD32Sfloat, IKIGAI::RENDER::PixelFormat::DEPTH32_FLOAT},
	{vk::Format::eX8D24UnormPack32, IKIGAI::RENDER::PixelFormat::DEPTH32_FLOAT_S8X24_UINT},
	{vk::Format::eB8G8R8A8Unorm, IKIGAI::RENDER::PixelFormat::BGRA_INT},
	{vk::Format::eD32SfloatS8Uint, IKIGAI::RENDER::PixelFormat::DEPTH_32_FLOAT_STENCIL_8_UINT},
	{ vk::Format::eR16G16B16A16Sfloat, IKIGAI::RENDER::PixelFormat::RGBA16_FLOAT },
	{ vk::Format::eR16G16Sfloat, IKIGAI::RENDER::PixelFormat::RG16_FLOAT },
	{ vk::Format::eR16Sfloat, IKIGAI::RENDER::PixelFormat::R16_FLOAT },
	{ vk::Format::eB10G11R11UfloatPack32, IKIGAI::RENDER::PixelFormat::R11G11B10_FLOAT },
};

// Optional formats (half-float storage, R11G11B10 storage) may lack some features: drop those usages
// instead of creating an invalid image. Sampling and transfer are required.
static vk::ImageUsageFlags SupportedColorUsage(vk::Format format, vk::ImageUsageFlags usage) {
	const auto features = IKIGAI::RENDER::UtilityVk::GetDriver()->mPhysicalDevice.getFormatProperties(format).optimalTilingFeatures;
	if (!(features & vk::FormatFeatureFlagBits::eSampledImage)) {
		throw std::runtime_error("Vulkan device cannot sample texture format " + vk::to_string(format));
	}
	if (!(features & vk::FormatFeatureFlagBits::eStorageImage)) {
		usage &= ~vk::ImageUsageFlags(vk::ImageUsageFlagBits::eStorage);
	}
	if (!(features & vk::FormatFeatureFlagBits::eColorAttachment)) {
		usage &= ~vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment);
	}
	return usage;
}

IKIGAI::RENDER::TextureVk::TextureVk(const TextureResource& descriptor, const std::vector<void*>& data, std::shared_ptr<VmaImage>* backing) {
	init(descriptor, data, backing);
}

IKIGAI::RENDER::AliasedRenderTargets IKIGAI::RENDER::DriverVk::createAliasedRenderTargets(std::span<const TextureResource> descriptors) {
	requireOwnerThread();
	const auto features = mPhysicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
	if (!features.get<vk::PhysicalDeviceVulkan13Features>().maintenance4) {
		return {};
	}
	AliasedRenderTargets result;
	std::shared_ptr<VmaImage> backing;
	result.Textures.reserve(descriptors.size());
	try {
		for (const auto& descriptor : descriptors) {
			result.Textures.push_back(AllocateTexture<TextureVk>(nullptr, {}, descriptor, std::vector<void*> {}, &backing));
		}
	} catch (const UnsupportedGpuFeature&) {
		return {};
	}
	if (backing) {
		VmaAllocationInfo info {};
		vmaGetAllocationInfo(mAllocator, backing->allocation(), &info);
		result.AllocationBytes = info.size;
	}
	return result;
}

void IKIGAI::RENDER::TextureVk::init(const TextureResource& descriptor, const std::vector<void*>& data, std::shared_ptr<VmaImage>* backing) {
	const TextureResource& desc = descriptor;
	const bool integerAttachment = desc.pixelType == PixelFormat::RG32_INT;
	if (integerAttachment) {
		ValidateRg32IntAttachment(desc, !data.empty());
	}
	if (desc.width == 0 || desc.height == 0) {
		throw std::invalid_argument(
			"Texture has zero width or height (failed to load?): " + (desc.pathTexture.empty() ? std::string("<memory>") : desc.pathTexture.front()));
	}

	auto usage =
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eTransferDst |
		vk::ImageUsageFlagBits::eTransferSrc |
		vk::ImageUsageFlagBits::eColorAttachment |
		vk::ImageUsageFlagBits::eStorage;
	if (desc.texType == TextureType::DEPTH) {
		usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
	}
	mType = desc.texType;
	if (desc.texType != TextureType::DEPTH) {
		usage = SupportedColorUsage(FormatMap.at(desc.pixelType), usage);
		if (integerAttachment
			&& (!(usage & vk::ImageUsageFlagBits::eColorAttachment) || (desc.storage && !(usage & vk::ImageUsageFlagBits::eStorage)))) {
			throw UnsupportedGpuFeature("Vulkan device lacks requested RG32_INT attachment usage");
		}
	}

	std::map<TextureType, vk::ImageViewType> ToTextureType = {
		{TextureType::TEXTURE_2D, vk::ImageViewType::e2D},
		{TextureType::TEXTURE_2D_ARRAY, vk::ImageViewType::e2DArray},
		{TextureType::TEXTURE_3D, vk::ImageViewType::e3D},
		{TextureType::TEXTURE_CUBE, vk::ImageViewType::eCube},
		{TextureType::DEPTH, vk::ImageViewType::e2D},
	};

	mFormat = desc.pixelType;
	mDepth = std::max(1, desc.depth);
	TextureInterface::mDepth = mDepth;
	mViewType = ToTextureType.at(desc.texType);
	std::tie(mImage, mImageView) = UtilityVk::CreateImage(
		desc.width, desc.height, FormatMap.at(desc.pixelType), usage, layoutAspects(), desc.depth, mViewType, desc.mipMapCount, backing);
	mImageUsage = usage;

	mImagePtr = *mImage;
	mPath = !desc.path.empty() ? desc.path : (!desc.pathTexture.empty() ? desc.pathTexture.front() : std::string());
#if IKIGAI_ENABLE_MEMORY_TRACKING
	mImage.setMemoryName(mPath.c_str());
#endif
	mWidth = desc.width;
	mHeight = desc.height;
	mMipCount = desc.mipMapCount > 0 ? static_cast<uint32_t>(desc.mipMapCount) : 1u;
	mLayouts.assign(static_cast<size_t>(mMipCount) * arrayLayerCount(), vk::ImageLayout::eUndefined);
	mDiscardAliasContents = false;

	// depth
	if (desc.texType == TextureType::DEPTH && !backing) {
		UtilityVk::OneTimeSubmit([&](auto& cmdbuf) { setState(cmdbuf, vk::ImageLayout::eDepthStencilAttachmentOptimal); });
	}

	///////////////////

	std::map<MinMagFilter, VkFilter> ToMigMagFilter = {
		{MinMagFilter::LINEAR, VK_FILTER_LINEAR},
		{MinMagFilter::NEAREST, VK_FILTER_NEAREST},
	};

	std::map<WrapFilter, VkSamplerAddressMode> ToWrapFilter = {
		{WrapFilter::CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER},
		{WrapFilter::CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
		{WrapFilter::MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT},
		{WrapFilter::MIRROR_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE},
		{WrapFilter::REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT},
	};


	VkSamplerCreateInfo samplerCreateInfo = {};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.magFilter = ToMigMagFilter.at(descriptor.magFilter);
	samplerCreateInfo.minFilter = ToMigMagFilter.at(descriptor.minFilter);;
	samplerCreateInfo.addressModeU = ToWrapFilter.at(descriptor.wrapS);
	samplerCreateInfo.addressModeV = ToWrapFilter.at(descriptor.wrapT);
	samplerCreateInfo.addressModeW = ToWrapFilter.at(descriptor.wrapR);
	//samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	//samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; //TODO: add to descriptor
	//samplerCreateInfo.mipLodBias = 0.0f;
	samplerCreateInfo.minLod = -1000.0f;
	samplerCreateInfo.maxLod = 1000.0f;
	//samplerCreateInfo.anisotropyEnable = VK_TRUE;
	samplerCreateInfo.maxAnisotropy = 1.0f;

	// Sampler state comes from the texture resource (OpenGL backend does the same), not hardcoded NEAREST without mips.
	const bool useMips = mMipCount > 1;
	auto sampler_create_info = vk::SamplerCreateInfo()
		.setMagFilter(static_cast<vk::Filter>(ToMigMagFilter.at(descriptor.magFilter)))
		.setMinFilter(static_cast<vk::Filter>(ToMigMagFilter.at(descriptor.minFilter)))
		.setMipmapMode(useMips ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest)
		.setAddressModeU(static_cast<vk::SamplerAddressMode>(ToWrapFilter.at(descriptor.wrapS)))
		.setAddressModeV(static_cast<vk::SamplerAddressMode>(ToWrapFilter.at(descriptor.wrapT)))
		.setAddressModeW(static_cast<vk::SamplerAddressMode>(ToWrapFilter.at(descriptor.wrapR)))
		.setMinLod(0.0f)
		.setMaxLod(useMips ? static_cast<float>(mMipCount) : 0.0f)
		.setMaxAnisotropy(1.0f);
	if (integerAttachment) {
		sampler_create_info.setBorderColor(vk::BorderColor::eIntTransparentBlack);
	}
	mSampler = UtilityVk::GetDriver()->mDevice.createSampler(sampler_create_info);

	if (!data.empty()) {
		setData(descriptor.width, descriptor.height, descriptor.pixelType, data, 0, 0, 0);
	}

	if (descriptor.useMipmap) {
		generateMips();
	}



	//VkDescriptorSet descriptor_set;
	//
	//VkDescriptorSetAllocateInfo set_alloc_info = {};
	//set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	//set_alloc_info.descriptorPool = UtilityVk::GetDriver()->im;
	//set_alloc_info.descriptorSetCount = 1;
	//set_alloc_info.pSetLayouts = &mTextureLayout;
	//auto result = vkAllocateDescriptorSets(*UtilityVk::GetDriver()->mDevice, &set_alloc_info, &descriptor_set);
	//if (result != VK_SUCCESS) {
	//	throw std::runtime_error("Failed to allocate Texture Descriptor Set!");
	//}
	//VkDescriptorPoolSize samplerPoolSize = {};
	//samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	//samplerPoolSize.descriptorCount = 20;
	//
	//VkDescriptorPoolCreateInfo samplerPoolCreateInfo = {};
	//samplerPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	//samplerPoolCreateInfo.maxSets = 20;
	//samplerPoolCreateInfo.poolSizeCount = 1;
	//samplerPoolCreateInfo.pPoolSizes = &samplerPoolSize;
	//
	//auto result = vkCreateDescriptorPool(*UtilityVk::GetDriver()->mDevice, &samplerPoolCreateInfo, nullptr, &mDescriptorSet);
	//if (result != VK_SUCCESS) {
	//	throw std::runtime_error("Failed to create a Descriptor Pool!");
	//}
	/*
	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	samplerLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo textureLayoutCreateInfo = {};
	textureLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	textureLayoutCreateInfo.bindingCount = 1;
	textureLayoutCreateInfo.pBindings = &samplerLayoutBinding;

	result = vkCreateDescriptorSetLayout(UtilityVk::GetDriver()->mDevice, &textureLayoutCreateInfo, nullptr, &mTextureLayout);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to create the Texture Descriptor Set Layout!");
	}

	VkDescriptorSetAllocateInfo set_alloc_info = {};
	set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc_info.descriptorPool = mTexDescriptorPool;
	set_alloc_info.descriptorSetCount = 1;
	set_alloc_info.pSetLayouts = &mTextureLayout;

	result = vkAllocateDescriptorSets(*UtilityVk::GetDriver()->mDevice, &set_alloc_info, &descriptor_set);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate Texture Descriptor Set!");
	}

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = *mImageView;
	imageInfo.sampler = texture->Sampler.back();

	VkWriteDescriptorSet descriptorWrite = {};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = descriptor_set;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(*UtilityVk::GetDriver()->mDevice, 1, &descriptorWrite, 0, nullptr);
	texture->descriptor_set = descriptor_set;*/
}

IKIGAI::RENDER::TextureVk::TextureVk(uint32_t width, uint32_t height, vk::Format format, vk::Image image) {
	mWidth = (width);
	mHeight = (height);
	mFormat = FormatMap2.at(format);
	mMipCount = 1;
	mImagePtr = (image);
	mImageView = UtilityVk::CreateImageView(image, format, vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::e2D);
}

IKIGAI::RENDER::TextureVk::~TextureVk() {
	releaseResources();
}

void IKIGAI::RENDER::TextureVk::releaseResources() {
    for(auto& [key,view]:mAttachmentViews) UtilityVk::GetDriver()->destroyDeferred(std::move(view));
    mAttachmentViews.clear();
	auto* driver = UtilityVk::GetDriver();
	// Editor draw data can still reference a texture replaced during resize.
	// Keep its descriptors, views and sampler alive until the frame fence.
	if (mDescriptorSet && driver->mImguiPool) {
		auto descriptor = std::shared_ptr<void>(reinterpret_cast<void*>(mDescriptorSet), [driver](void* pointer) {
			if (!driver->mImguiPool) return; // ImGui shutdown already destroyed the pool.
			const auto set = reinterpret_cast<VkDescriptorSet>(pointer);
			vkFreeDescriptorSets(*driver->mDevice, driver->mImguiPool, 1, &set);
		});
		driver->destroyDeferred(std::move(descriptor));
	}
	mDescriptorSet = nullptr;
	if (*mBaseMipStorageView) {
		driver->destroyDeferred(std::move(mBaseMipStorageView));
	}
	if (*mImageView) driver->destroyDeferred(std::move(mImageView));
	if (*mSampler) driver->destroyDeferred(std::move(mSampler));
	if (mImage) driver->destroyDeferred(std::move(mImage));
}

void IKIGAI::RENDER::TextureVk::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
	if (descriptor.pixelType == PixelFormat::RG32_INT) {
		ValidateRg32IntAttachment(descriptor, !fileData.empty());
	}
	requireOwnerThread();
	++mRevision;
	// Освобождаем старые VK-ресурсы (отложенно)
	releaseResources();

	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	bool needFree = false;

	if (_d.colorData.empty() && !fileData.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		for (const auto& fData : fileData) {
			int w = 0, h = 0, c = 0;
			if (_d.isFloat) {
				textureData.push_back(IKIGAI::UTILS::STBiLoadfFromMemory(fData.data(), fData.size(), &w, &h, &c, 4)); c=4;
			} else {
				auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 0);
				if (data && c != 4) {
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

	if (_d.depth == 0) _d.depth = 1;
	if (_d.useMipmap) _d.mipMapCount = GetMipCount(_d.width, _d.height);
	else _d.mipMapCount = std::max(1,_d.mipMapCount);

	init(descriptor, textureData);

	if (needFree) {
		for (auto* ptr : textureData) {
			if (descriptor.isFloat) IKIGAI::UTILS::STBiImageFree((float*)ptr);
			else IKIGAI::UTILS::STBiImageFree((unsigned char*)ptr);
		}
	}
}

void* IKIGAI::RENDER::TextureVk::getImguiId() {
	if (!mDescriptorSet) {
		if (auto* imgui = IKIGAI::IMGUI::Get()) {
			imgui->registerGpuTexture(*this);
		}
	}
	auto* driver = UtilityVk::GetDriver();
	if (!isInState(vk::ImageLayout::eGeneral)) {
		driver->deactivateRenderPass();
	}
	setState(driver->getCurrentFrame().mCommandBuffer, vk::ImageLayout::eGeneral);
	return (void*)mDescriptorSet;
}



std::shared_ptr<IKIGAI::RENDER::TextureVk> IKIGAI::RENDER::TextureVk::Create(const std::string& path, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	IKIGAI::RENDER::TextureResource res;
	res.useMipmap = true;
	res.pathTexture.push_back(path);
	return Create(res, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureVk> IKIGAI::RENDER::TextureVk::Create(const TextureResource& descriptor, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	if (descriptor.pixelType == PixelFormat::RG32_INT) {
		ValidateRg32IntAttachment(descriptor, false);
		return AllocateTexture<TextureVk>(allocator, deleter, descriptor, std::vector<void*> {});
	}
	auto& _descriptor = const_cast<TextureResource&>(descriptor);
	//Load data
	std::vector<void*> textureData;
	if (!_descriptor.pathTexture.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		for (const auto& path : _descriptor.pathTexture) {
			int width = 0, height = 0, channels = 0;
			
			auto file = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>().getFile(path, IKIGAI::RESOURCES::FileMode::READ);
			if (!file) continue;
			auto fileData = file->read();
			
			if (_descriptor.isFloat) {
				auto* data = IKIGAI::UTILS::STBiLoadfFromMemory(fileData.data(), fileData.size(), &width, &height, &channels, 4); channels=4;
				if (data) textureData.push_back(data);
			} else {
				auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fileData.data(), fileData.size(), &width, &height, &channels, 0);
				if (data && channels != 4) {
					UTILS::STBiImageFree((unsigned char*)data);
					data = IKIGAI::UTILS::STBiLoadFromMemory(fileData.data(), fileData.size(), &width, &height, &channels, 4);
					channels = 4;
				}
				if (data) textureData.push_back(data);
			}
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
	}
	else {
		_descriptor.mipMapCount = std::max(1,_descriptor.mipMapCount);
	}

	//Create texture
	auto tex = AllocateTexture<TextureVk>(allocator, deleter, descriptor, textureData);

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

std::shared_ptr<IKIGAI::RENDER::TextureVk> IKIGAI::RENDER::TextureVk::Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	if (descriptor.pixelType == PixelFormat::RG32_INT) {
		ValidateRg32IntAttachment(descriptor, !fileData.empty());
		return AllocateTexture<TextureVk>(allocator, deleter, descriptor, std::vector<void*> {});
	}
	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	bool needFree = false;

	if (_d.colorData.empty() && !fileData.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		for (const auto& fData : fileData) {
			int w = 0, h = 0, c = 0;
			if (_d.isFloat) {
				textureData.push_back(IKIGAI::UTILS::STBiLoadfFromMemory(fData.data(), fData.size(), &w, &h, &c, 4)); c=4;
			} else {
				auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 0);
				if (data && c != 4) {
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
	if (_d.useMipmap) _d.mipMapCount = GetMipCount(_d.width, _d.height);
	else _d.mipMapCount = std::max(1,_d.mipMapCount);

	auto tex = AllocateTexture<TextureVk>(allocator, deleter, descriptor, textureData);

	if (needFree) {
		for (auto* ptr : textureData) {
			if (descriptor.isFloat) IKIGAI::UTILS::STBiImageFree((float*)ptr);
			else IKIGAI::UTILS::STBiImageFree((unsigned char*)ptr);
		}
	}
	return tex;
}

// --------------- TextureAtlasVk ---------------

IKIGAI::RENDER::AtlasRect IKIGAI::RENDER::TextureAtlasVk::getPiece(const std::string& name) const {
	if (mAtlas.mRects.contains(name)) return mAtlas.mRects.at(name);
	return IKIGAI::RENDER::AtlasRect{};
}

IKIGAI::RENDER::AtlasRect IKIGAI::RENDER::TextureAtlasVk::getPieceUV(const std::string& name) const {
	if (mAtlas.mRects.contains(name)) {
		auto res = mAtlas.mRects.at(name);
		res.mX /= mWidth; res.mY /= mHeight;
		res.mW /= mWidth; res.mH /= mHeight;
		return res;
	}
	return IKIGAI::RENDER::AtlasRect{};
}

void IKIGAI::RENDER::TextureAtlasVk::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
	TextureVk::recreate(descriptor, fileData);
}

std::shared_ptr<IKIGAI::RENDER::TextureAtlasVk> IKIGAI::RENDER::TextureAtlasVk::CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	TextureResource res;
	res.useMipmap = generateMipmap;
	res.pathTexture.push_back(path);
	return CreateAtlasFromResource(res, allocator, deleter);
}

std::shared_ptr<IKIGAI::RENDER::TextureAtlasVk> IKIGAI::RENDER::TextureAtlasVk::CreateAtlasFromResource(const TextureResource& descriptor, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	if (!_d.pathTexture.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		const auto& path = _d.pathTexture[0];
		int w = 0, h = 0, c = 0;
		auto* data = IKIGAI::UTILS::STBiLoad(path.c_str(), &w, &h, &c, 0);
		if (data && c != 4) {
			IKIGAI::UTILS::STBiImageFree((unsigned char*)data);
			data = IKIGAI::UTILS::STBiLoad(path.c_str(), &w, &h, &c, 4); c = 4;
		}
		textureData.push_back(data);
		_d.width = w; _d.height = h; _d.channels = c;
	} else if (!_d.colorData.empty()) {
		textureData.push_back((void*)_d.colorData.data());
	}
	if (_d.depth == 0) _d.depth = 1;
	if (_d.useMipmap) _d.mipMapCount = GetMipCount(_d.width, _d.height);
	else _d.mipMapCount = std::max(1,_d.mipMapCount);

	auto tex = AllocateTexture<TextureAtlasVk>(allocator, deleter, descriptor, textureData);

	if (!_d.pathTexture.empty() && !textureData.empty()) {
		IKIGAI::UTILS::STBiImageFree((unsigned char*)textureData[0]);
	}

	// Загружаем .atlas JSON
	if (!_d.pathTexture.empty()) {
		std::filesystem::path configPath{ _d.pathTexture[0] };
		configPath.replace_extension(".atlas");
		auto atlasRes = IKIGAI::UTILS::FromJson<IKIGAI::RENDER::AtlasData>(configPath.string());
		if (atlasRes.isOk()) tex->mAtlas = atlasRes.unwrap();
	}
	return tex;
}

std::shared_ptr<IKIGAI::RENDER::TextureAtlasVk> IKIGAI::RENDER::TextureAtlasVk::CreateAtlasFromResource(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) {
	auto& _d = const_cast<TextureResource&>(descriptor);
	std::vector<void*> textureData;
	bool needFree = false;

	if (_d.colorData.empty() && !fileData.empty()) {
		IKIGAI::UTILS::STBiSetFlipVerticallyOnLoad(true);
		const auto& fData = fileData[0];
		int w = 0, h = 0, c = 0;
		auto* data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 0);
		if (data && c != 4) {
			IKIGAI::UTILS::STBiImageFree((unsigned char*)data);
			data = IKIGAI::UTILS::STBiLoadFromMemory(fData.data(), fData.size(), &w, &h, &c, 4); c = 4;
		}
		textureData.push_back(data);
		_d.width = w; _d.height = h; _d.channels = c;
		needFree = true;
	} else if (!_d.colorData.empty()) {
		textureData.push_back((void*)_d.colorData.data());
	}
	if (_d.depth == 0) _d.depth = 1;
	if (_d.useMipmap) _d.mipMapCount = GetMipCount(_d.width, _d.height);
	else _d.mipMapCount = std::max(1,_d.mipMapCount);

	auto tex = AllocateTexture<TextureAtlasVk>(allocator, deleter, descriptor, textureData);

	if (needFree && !textureData.empty()) {
		IKIGAI::UTILS::STBiImageFree((unsigned char*)textureData[0]);
	}

	if (!_d.pathTexture.empty()) {
		std::filesystem::path configPath{ _d.pathTexture[0] };
		configPath.replace_extension(".atlas");
		auto atlasRes = IKIGAI::UTILS::FromJson<IKIGAI::RENDER::AtlasData>(configPath.string());
		if (atlasRes.isOk()) tex->mAtlas = atlasRes.unwrap();
	}
	return tex;
}

static uint32_t GetFormatChannelsCount(IKIGAI::RENDER::PixelFormat format) {
	static const std::unordered_map<IKIGAI::RENDER::PixelFormat, uint32_t> FormatChannelsMap = {
		{IKIGAI::RENDER::PixelFormat::R_FLOAT, 1},
		{IKIGAI::RENDER::PixelFormat::RG_FLOAT, 2},
		{ IKIGAI::RENDER::PixelFormat::RG32_INT, 2 },
		{IKIGAI::RENDER::PixelFormat::RGB_FLOAT, 3},
		{IKIGAI::RENDER::PixelFormat::RGBA_FLOAT, 4},
		{IKIGAI::RENDER::PixelFormat::R_INT, 1},
		{IKIGAI::RENDER::PixelFormat::RG_INT, 2},
		{IKIGAI::RENDER::PixelFormat::RGB_INT, 3},
		{IKIGAI::RENDER::PixelFormat::RGBA_INT, 4},
		{IKIGAI::RENDER::PixelFormat::RGBA16_FLOAT, 4},
		{IKIGAI::RENDER::PixelFormat::RG16_FLOAT, 2},
		{IKIGAI::RENDER::PixelFormat::R16_FLOAT, 1},
		{IKIGAI::RENDER::PixelFormat::R11G11B10_FLOAT, 1}
	};
	return FormatChannelsMap.at(format);
}

static uint32_t GetFormatChannelSize(IKIGAI::RENDER::PixelFormat format) {
	static const std::unordered_map<IKIGAI::RENDER::PixelFormat, uint32_t> FormatChannelSizeMap = {
		{IKIGAI::RENDER::PixelFormat::R_FLOAT, 4},
		{IKIGAI::RENDER::PixelFormat::RG_FLOAT, 4},
		{ IKIGAI::RENDER::PixelFormat::RG32_INT, 4 },
		{IKIGAI::RENDER::PixelFormat::RGB_FLOAT, 4},
		{IKIGAI::RENDER::PixelFormat::RGBA_FLOAT, 4},
		{IKIGAI::RENDER::PixelFormat::R_INT, 1},
		{IKIGAI::RENDER::PixelFormat::RG_INT, 1},
		{IKIGAI::RENDER::PixelFormat::RGB_INT, 1},
		{IKIGAI::RENDER::PixelFormat::RGBA_INT, 1},
		{IKIGAI::RENDER::PixelFormat::RGBA16_FLOAT, 2},
		{IKIGAI::RENDER::PixelFormat::RG16_FLOAT, 2},
		{IKIGAI::RENDER::PixelFormat::R16_FLOAT, 2},
		{IKIGAI::RENDER::PixelFormat::R11G11B10_FLOAT, 4}
	};
	return FormatChannelSizeMap.at(format);
}

void IKIGAI::RENDER::TextureVk::setData(uint32_t width, uint32_t height, PixelFormat format, const std::vector<void*>& data,
	uint32_t mip_level, uint32_t offset_x, uint32_t offset_y) {
	requireOwnerThread();
	UtilityVk::GetDriver()->deactivateRenderPass();

	auto channels = GetFormatChannelsCount(format);
	auto channel_size = GetFormatChannelSize(format);
	auto size = width * height * channels * channel_size;

	auto upload_buffer = UtilityVk::CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, UtilityVk::MemoryUsage::Staging);

	setState(UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer, vk::ImageLayout::eTransferDstOptimal, mip_level, 0);

	int i = 0;
	//std::vector<vk::BufferImageCopy> regions;
	for (auto& memory : data) {
		upload_buffer.upload(memory, size);

		auto image_subresource_layers = vk::ImageSubresourceLayers()
			.setAspectMask(vk::ImageAspectFlagBits::eColor)
			.setMipLevel(mip_level)
			.setLayerCount(1);// .setBaseArrayLayer(i);

		auto region = vk::BufferImageCopy()
			.setImageSubresource(image_subresource_layers)
			.setImageExtent({width, height, 1});
		//regions.push_back(region);

		UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.copyBufferToImage(*upload_buffer, mImagePtr,
			vk::ImageLayout::eTransferDstOptimal, {region});

		++i;
	}

	//UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.copyBufferToImage(*upload_buffer, mImagePtr,
	//	vk::ImageLayout::eTransferDstOptimal, regions);

	UtilityVk::GetDriver()->destroyDeferred(std::move(upload_buffer));
}

void IKIGAI::RENDER::TextureVk::generateMips() {
	if (mFormat == PixelFormat::RG32_INT) {
		throw std::invalid_argument("RG32_INT does not support automatic mip generation");
	}
	auto* driver = UtilityVk::GetDriver();
	driver->deactivateRenderPass();
	const auto& commands = driver->getCurrentFrame().mCommandBuffer;
	setState(commands, vk::ImageLayout::eTransferSrcOptimal, 0, 0);
	for (uint32_t mip = 1; mip < mMipCount; ++mip) {
		setState(commands, vk::ImageLayout::eTransferDstOptimal, mip, 0);
		const auto src = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip - 1, 0, 1);
		const auto dst = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, mip, 0, 1);
		const auto extent = [&](uint32_t level) {
			return vk::Offset3D {
				static_cast<int32_t>(std::max<size_t>(1, mWidth >> level)),
				static_cast<int32_t>(std::max<size_t>(1, mHeight >> level)),
				1,
			};
		};
		const auto region = vk::ImageBlit()
								.setSrcSubresource(src)
								.setDstSubresource(dst)
								.setSrcOffsets({ vk::Offset3D {}, extent(mip - 1) })
								.setDstOffsets({ vk::Offset3D {}, extent(mip) });
		commands.blitImage(
			mImagePtr, vk::ImageLayout::eTransferSrcOptimal, mImagePtr, vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eLinear);
		setState(commands, vk::ImageLayout::eTransferSrcOptimal, mip, 0);
	}
}

vk::ImageView IKIGAI::RENDER::TextureVk::attachmentView(unsigned mip, unsigned layer) {
	const auto key = std::make_pair(mip, layer);
	if (!mAttachmentViews.contains(key)) {
		const auto info = vk::ImageViewCreateInfo()
							  .setImage(mImagePtr)
							  .setFormat(FormatMap.at(mFormat))
							  .setViewType(vk::ImageViewType::e2D)
							  .setSubresourceRange(subresourceRange(mip, layer));
		mAttachmentViews.emplace(key, UtilityVk::GetDriver()->mDevice.createImageView(info));
	}
	return *mAttachmentViews.at(key);
}

vk::ImageView IKIGAI::RENDER::TextureVk::baseMipStorageView() {
	if (mMipCount == 1) {
		return *mImageView;
	}
	if (mViewType == vk::ImageViewType::e2D) {
		return attachmentView(0, 0);
	}
	if (!*mBaseMipStorageView) {
		mBaseMipStorageView = UtilityVk::CreateImageView(mImagePtr, FormatMap.at(mFormat), layoutAspects(), mDepth, mViewType);
	}
	return *mBaseMipStorageView;
}

vk::ImageAspectFlags IKIGAI::RENDER::TextureVk::layoutAspects() const {
	if (mType != TextureType::DEPTH) {
		return vk::ImageAspectFlagBits::eColor;
	}
	if (mFormat == PixelFormat::DEPTH32_FLOAT) {
		return vk::ImageAspectFlagBits::eDepth;
	}
	// Packed depth/stencil aspects share a layout without separateDepthStencilLayouts.
	return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
}

uint32_t IKIGAI::RENDER::TextureVk::arrayLayerCount() const {
	switch (mType) {
	case TextureType::TEXTURE_CUBE:
		return 6;
	case TextureType::TEXTURE_2D_ARRAY:
		return static_cast<uint32_t>(mDepth);
	default:
		return 1;
	}
}

size_t IKIGAI::RENDER::TextureVk::layoutIndex(uint32_t mip, uint32_t layer) const {
	const auto layerCount = arrayLayerCount();
	if (mip >= mMipCount || layer >= layerCount) {
		throw std::out_of_range("Vulkan texture subresource out of range");
	}
	return static_cast<size_t>(mip) * layerCount + layer;
}

vk::ImageLayout IKIGAI::RENDER::TextureVk::state(uint32_t mip, uint32_t layer) const {
	return mLayouts[layoutIndex(mip, layer)];
}

bool IKIGAI::RENDER::TextureVk::isInState(vk::ImageLayout state) const {
	return std::ranges::all_of(mLayouts, [state](vk::ImageLayout layout) { return layout == state; });
}

vk::ImageSubresourceRange IKIGAI::RENDER::TextureVk::subresourceRange(uint32_t mip, uint32_t layer) const {
	return vk::ImageSubresourceRange(layoutAspects(), mip, 1, layer, 1);
}

void IKIGAI::RENDER::TextureVk::resetAliasedState(const vk::raii::CommandBuffer& cmdbuf) {
	const auto barrier = vk::ImageMemoryBarrier2()
							 .setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
							 .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
							 .setDstAccessMask(vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite)
							 .setOldLayout(vk::ImageLayout::eUndefined)
							 .setNewLayout(vk::ImageLayout::eGeneral)
							 .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
							 .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
							 .setImage(mImagePtr)
							 .setSubresourceRange(vk::ImageSubresourceRange(layoutAspects(), 0, mMipCount, 0, arrayLayerCount()));
	cmdbuf.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(barrier));
	std::ranges::fill(mLayouts, vk::ImageLayout::eGeneral);
	mDiscardAliasContents = true;
}

void IKIGAI::RENDER::TextureVk::setState(const vk::raii::CommandBuffer& cmdbuf, vk::ImageLayout state, uint32_t mip, uint32_t layer) {
	auto& oldState = mLayouts[layoutIndex(mip, layer)];
	if (oldState != state) {
		UtilityVk::SetImageMemoryBarrier(cmdbuf, mImagePtr, layoutAspects(), oldState, state, mip, 1, layer, 1);
		oldState = state;
	}
}

void IKIGAI::RENDER::TextureVk::applyImageBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::ImageMemoryBarrier2 barrier) {
	const auto aspects = layoutAspects();
	const auto range = barrier.subresourceRange;
	if (!(aspects & range.aspectMask)) {
		throw std::invalid_argument("Vulkan graph image barrier aspect does not match the texture");
	}
	const auto layers = arrayLayerCount();
	if (range.baseMipLevel >= mMipCount || range.baseArrayLayer >= layers) {
		throw std::out_of_range("Vulkan graph image barrier starts outside the texture");
	}
	const auto remainingMips = mMipCount - range.baseMipLevel;
	const auto remainingLayers = layers - range.baseArrayLayer;
	const auto mipCount = range.levelCount == VK_REMAINING_MIP_LEVELS ? remainingMips : range.levelCount;
	const auto layerCount = range.layerCount == VK_REMAINING_ARRAY_LAYERS ? remainingLayers : range.layerCount;
	if (mipCount > remainingMips || layerCount > remainingLayers) {
		throw std::out_of_range("Vulkan graph image barrier extends outside the texture");
	}

	const auto mipEnd = range.baseMipLevel + mipCount;
	const auto layerEnd = range.baseArrayLayer + layerCount;
	std::vector<vk::ImageMemoryBarrier2> barriers;
	barriers.reserve(static_cast<size_t>(mipCount) * layerCount);
	barrier.setImage(mImagePtr);
	for (auto mip = range.baseMipLevel; mip < mipEnd; ++mip) {
		for (auto layer = range.baseArrayLayer; layer < layerEnd; ++layer) {
			// Equal layouts still need the dependency. Packed depth/stencil uses the native aspect mask.
			barriers.push_back(barrier.setOldLayout(state(mip, layer)).setSubresourceRange(vk::ImageSubresourceRange(aspects, mip, 1, layer, 1)));
		}
	}
	cmdbuf.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(barriers));
	for (auto mip = range.baseMipLevel; mip < mipEnd; ++mip) {
		for (auto layer = range.baseArrayLayer; layer < layerEnd; ++layer) {
			mLayouts[layoutIndex(mip, layer)] = barrier.newLayout;
		}
	}
}

void IKIGAI::RENDER::TextureVk::setState(const vk::raii::CommandBuffer& cmdbuf, vk::ImageLayout state) {
	const auto oldState = mLayouts.front();
	if (isInState(oldState)) {
		if (oldState != state) {
			UtilityVk::SetImageMemoryBarrier(cmdbuf, mImagePtr, layoutAspects(), oldState, state, 0, mMipCount, 0, arrayLayerCount());
			std::ranges::fill(mLayouts, state);
		}
		return;
	}
	for (uint32_t mip = 0; mip < mMipCount; ++mip) {
		for (uint32_t layer = 0; layer < arrayLayerCount(); ++layer) {
			setState(cmdbuf, state, mip, layer);
		}
	}
}

#endif
