#pragma once
#ifdef DX12_BACKEND
#include <memory>
#include <string>
#include <wrl/client.h>

#include <optional>
#include <vector>

#include "d3dUtil.h"
#include "d3dx12/d3dx12.h"
#include "renderModule/backends/interface/renderEnums.h"
#include <d3d12.h>
#include "../interface/textureInterface.h"
#include "../interface/driverInterface.h"
#include "../interface/atlasInterface.h"
#include "utilsModule/memory/memory.h"

namespace IKIGAI::RENDER {
	struct TextureResource;
	struct AliasHeapDx12;

	class TextureDx12 : public TextureInterface, public std::enable_shared_from_this<TextureDx12> {
	public:
		TextureDx12(const TextureResource& descriptor, const std::vector<void*>& data, std::shared_ptr<AliasHeapDx12>* backing = nullptr);
		TextureDx12(size_t width, size_t height, PixelFormat format, Microsoft::WRL::ComPtr<ID3D12Resource> texture);
		~TextureDx12() override;

		void setData(const std::vector<void*>& data, size_t width, size_t height, PixelFormat format, size_t mipLevel);
		void generateMips();

		void* getImguiId() override;
		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		const Dx12Resource& getResource() const;
		CD3DX12_GPU_DESCRIPTOR_HANDLE getGpuDescriptorHandle() const;
        CD3DX12_GPU_DESCRIPTOR_HANDLE getUavHandle() const { return mUavHandle; }
		void setState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state);
		void setSampledState(ID3D12GraphicsCommandList* cmdList, UINT depthMipCount = 1);
		void setPlaneZeroState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state, UINT firstMip, UINT mipCount, UINT firstLayer,
			UINT layerCount, bool orderUavWrites);
		void setSubresourceState(ID3D12GraphicsCommandList* cmdList, UINT subresource, D3D12_RESOURCE_STATES state);
		D3D12_RESOURCE_STATES getSubresourceState(UINT subresource) const;
		UINT subresourceIndex(UINT mip, UINT layer, UINT plane = 0) const;
		UINT arrayLayerCount() const {
			return mArraySize;
		}

		static std::shared_ptr<TextureDx12> Create(const std::string& path, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureDx12> Create(const TextureResource& descriptor, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureDx12> Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);

	private:
		void generateMips(ID3D12GraphicsCommandList* cmdlist);
		void create(const TextureResource& descriptor, const std::vector<void*>& data, std::shared_ptr<AliasHeapDx12>* backing = nullptr);
		void initSubresourceStates();

		Dx12Resource mResource;
		uint32_t mDescriptorIndex = UINT32_MAX;
		uint32_t mDescriptorCount = 0;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuDescriptorHandle;
        CD3DX12_GPU_DESCRIPTOR_HANDLE mUavHandle{};
		UINT mArraySize = 1;
		UINT mPlaneCount = 1;
		D3D12_RESOURCE_STATES mUniformState = D3D12_RESOURCE_STATE_COMMON;
		std::vector<D3D12_RESOURCE_STATES> mSubresourceStates;
	};

	class TextureAtlasDx12 : public TextureDx12 {
		AtlasData mAtlas;
	public:
		TextureAtlasDx12() = default;
		TextureAtlasDx12(const TextureResource& descriptor, const std::vector<void*>& data)
			: TextureDx12(descriptor, data) {}

		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		[[nodiscard]] AtlasRect getPiece(const std::string& name) const;
		[[nodiscard]] AtlasRect getPieceUV(const std::string& name) const;

		static std::shared_ptr<TextureAtlasDx12> CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasDx12> CreateAtlasFromResource(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasDx12> CreateAtlasFromResource(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
	};
}
#endif
