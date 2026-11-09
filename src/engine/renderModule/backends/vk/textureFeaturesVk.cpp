#include "driverVk.h"
#ifdef VULKAN_BACKEND
#include <array>
#include <stdexcept>
#include <string>

#include "textureVk.h"

namespace IKIGAI::RENDER {
namespace {
	constexpr std::array<vk::Format, 4> HalfFloatFormats = {
		vk::Format::eR16G16B16A16Sfloat,
		vk::Format::eR16G16Sfloat,
		vk::Format::eR16Sfloat,
		vk::Format::eB10G11R11UfloatPack32,
	};

	// Optional-storage formats; TextureVk drops eStorage usage when the device lacks it.
	vk::Format OptionalStorageFormat(PixelFormat format) {
		switch (format) {
		case PixelFormat::RGBA16_FLOAT:
			return vk::Format::eR16G16B16A16Sfloat;
		case PixelFormat::RG16_FLOAT:
			return vk::Format::eR16G16Sfloat;
		case PixelFormat::R16_FLOAT:
			return vk::Format::eR16Sfloat;
		case PixelFormat::R11G11B10_FLOAT:
			return vk::Format::eB10G11R11UfloatPack32;
		default:
			return vk::Format::eUndefined;
		}
	}
}

void DriverVk::setStorageImage(size_t binding, std::shared_ptr<TextureInterface> texture, uint32_t mip, uint32_t layer) {
	auto native = std::static_pointer_cast<TextureVk>(texture);
	if (!native) {
		throw std::invalid_argument("setStorageImage: texture is null");
	}
	const auto type = native->getType();
	if (type != TextureType::TEXTURE_2D && type != TextureType::TEXTURE_2D_ARRAY && type != TextureType::TEXTURE_CUBE) {
		throw UnsupportedGpuFeature("setStorageImage supports 2D, 2D array and cube textures only");
	}
	if (mip >= native->getMipCount()) {
		throw std::out_of_range("setStorageImage: mip " + std::to_string(mip) + " >= mip count " + std::to_string(native->getMipCount()));
	}
	const auto layers = native->arrayLayerCount();
	if (layer >= layers) {
		throw std::out_of_range("setStorageImage: layer " + std::to_string(layer) + " >= layer count " + std::to_string(layers));
	}
	const auto optional = OptionalStorageFormat(native->getFormat());
	if (optional != vk::Format::eUndefined
		&& !(mPhysicalDevice.getFormatProperties(optional).optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage)) {
		throw UnsupportedGpuFeature("setStorageImage: device has no storage image support for " + vk::to_string(optional));
	}
	mTextures[binding] = native;
	mStorageImageBindings[binding] = StorageImageBinding { mip, layer };
	graphics_pipeline_ignore_bindings.erase(static_cast<uint32_t>(binding));
}

void DriverVk::fillTextureGpuFeatures(GpuFeatures& features) const {
	features.StorageImageMipViews = true;
	bool halfFloat = true;
	for (const auto format : HalfFloatFormats) {
		const auto tiling = mPhysicalDevice.getFormatProperties(format).optimalTilingFeatures;
		const auto required = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear
			| vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eStorageImage;
		halfFloat = halfFloat && (tiling & required) == required;
	}
	features.HalfFloatTextures = halfFloat;
	// Enabled at device creation when the physical device supports it.
	features.SamplerMinMaxReduction = mRTCoreFeatures.samplerFilterMinmax == VK_TRUE;
}
}
#endif
