#include "driverDx12.h"
#ifdef DX12_BACKEND
#include <array>
#include <stdexcept>
#include <string>

#include "textureDx12.h"

namespace IKIGAI::RENDER {
namespace {
	constexpr std::array<DXGI_FORMAT, 4> HalfFloatFormats = {
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16_FLOAT,
		DXGI_FORMAT_R16_FLOAT,
		DXGI_FORMAT_R11G11B10_FLOAT,
	};
}

void DriverDx12::setStorageImage(size_t binding, std::shared_ptr<TextureInterface> texture, uint32_t mip, uint32_t layer) {
	auto native = std::static_pointer_cast<TextureDx12>(texture);
	if (!native) {
		throw std::invalid_argument("setStorageImage: texture is null");
	}
	const auto type = native->getType();
	if (type != TextureType::TEXTURE_2D && type != TextureType::TEXTURE_2D_ARRAY && type != TextureType::TEXTURE_CUBE) {
		throw UnsupportedGpuFeature("setStorageImage supports 2D, 2D array and cube textures only");
	}
	const auto desc = native->getResource().Get()->GetDesc();
	if (!(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
		throw UnsupportedGpuFeature("setStorageImage: DX12 texture was not created with TextureResource::storage = true");
	}
	if (mip >= desc.MipLevels) {
		throw std::out_of_range("setStorageImage: mip " + std::to_string(mip) + " >= mip count " + std::to_string(desc.MipLevels));
	}
	if (layer >= desc.DepthOrArraySize) {
		throw std::out_of_range("setStorageImage: layer " + std::to_string(layer) + " >= layer count " + std::to_string(desc.DepthOrArraySize));
	}
	mTextures[binding] = native;
	mStorageImageBindings[binding] = StorageImageBinding { native, mip, layer };
}

D3D12_GPU_DESCRIPTOR_HANDLE DriverDx12::storageImageUav(const StorageImageBinding& binding) {
	auto* resource = binding.Texture->getResource().Get();
	const auto subresource = binding.Texture->subresourceIndex(binding.Mip, binding.Layer);
	binding.Texture->setSubresourceState(mCommandList.Get(), subresource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	// Cached per frame next to the bindless snapshots, which share the transient descriptor range.
	const std::string key = "storage-image:" + std::to_string(reinterpret_cast<uintptr_t>(resource)) + ":" + std::to_string(binding.Mip) + ":"
		+ std::to_string(binding.Layer);
	if (const auto found = mBindlessSnapshots.find(key); found != mBindlessSnapshots.end()) {
		return found->second.first;
	}
	if (mBindlessDescriptorCursor >= mBindlessDescriptorEnd) {
		throw std::runtime_error("Storage image descriptor capacity exhausted in current frame");
	}
	auto cpu = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += SIZE_T(mBindlessDescriptorCursor) * mDescriptorIncSize;
	auto gpu = mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += UINT64(mBindlessDescriptorCursor) * mDescriptorIncSize;
	++mBindlessDescriptorCursor;

	const auto desc = resource->GetDesc();
	D3D12_UNORDERED_ACCESS_VIEW_DESC view {};
	view.Format = desc.Format;
	if (desc.DepthOrArraySize > 1 || binding.Texture->getType() == TextureType::TEXTURE_2D_ARRAY) {
		view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		view.Texture2DArray.MipSlice = binding.Mip;
		view.Texture2DArray.FirstArraySlice = binding.Layer;
		view.Texture2DArray.ArraySize = 1;
	}
	else {
		view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		view.Texture2D.MipSlice = binding.Mip;
	}
	mDevice->CreateUnorderedAccessView(resource, nullptr, &view, cpu);
	// Keep the texture alive until the frame's descriptors are no longer referenced.
	mBindlessFrameResources.push_back({ binding.Texture, {} });
	mBindlessSnapshots.emplace(key, std::make_pair(gpu, D3D12_GPU_DESCRIPTOR_HANDLE {}));
	return gpu;
}

void DriverDx12::fillTextureGpuFeatures(GpuFeatures& features) const {
	features.StorageImageMipViews = true;
	bool halfFloat = true;
	for (const auto format : HalfFloatFormats) {
		D3D12_FEATURE_DATA_FORMAT_SUPPORT support { format };
		if (FAILED(mDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) {
			halfFloat = false;
			continue;
		}
		const auto required1 = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_RENDER_TARGET
			| D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
		halfFloat = halfFloat && (support.Support1 & required1) == required1 && (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
	}
	features.HalfFloatTextures = halfFloat;
	// D3D12 exposes MIN/MAX reduction filters together with tiled resources tier 2.
	D3D12_FEATURE_DATA_D3D12_OPTIONS options {};
	features.SamplerMinMaxReduction = SUCCEEDED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))
		&& options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2;
}
}
#endif
