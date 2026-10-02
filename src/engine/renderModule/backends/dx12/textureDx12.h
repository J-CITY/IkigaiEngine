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
#include "utilsModule/memoryAlloc.h"

namespace IKIGAI::RENDER {
	struct TextureResource;

	class TextureDx12 : public TextureInterface {
	public:
		TextureDx12(const TextureResource& descriptor, const std::vector<void*>& data);
		TextureDx12(size_t width, size_t height, PixelFormat format, Microsoft::WRL::ComPtr<ID3D12Resource> texture);
		~TextureDx12() override;

		void setData(const std::vector<void*>& data, size_t width, size_t height, PixelFormat format, size_t mipLevel);
		void generateMips();

		void* getImguiId() override;
		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		const Dx12Resource& getResource() const;
		CD3DX12_GPU_DESCRIPTOR_HANDLE getGpuDescriptorHandle() const;
		void setState(ID3D12GraphicsCommandList* cmdlist, D3D12_RESOURCE_STATES state);

		static std::shared_ptr<TextureDx12> Create(const std::string& path, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureDx12> Create(const TextureResource& descriptor, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureDx12> Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);

	private:
		void generateMips(ID3D12GraphicsCommandList* cmdlist);
		void create(const TextureResource& descriptor, const std::vector<void*>& data);

		Dx12Resource mResource;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuDescriptorHandle;
		D3D12_RESOURCE_STATES mCurrentState = D3D12_RESOURCE_STATE_COMMON;
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