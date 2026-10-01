#pragma once
#ifdef DX12_BACKEND
#include <bitset>
#include <dxgi1_6.h>
#include <map>

#include "frameBufferDx12.h"
#include "shaderDx12.h"
#include "d3dx12/d3dx12.h"
#include "renderModule/backends/interface/renderEnums.h"
#include "renderModule/backends/interface/textureInterface.h"
#include <array>
#include <d3d12.h>
#include <wrl/client.h>
#include "../interface/driverInterface.h"

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"

namespace IKIGAI::RENDER {
	class StorageBufferDx12;
	class StorageBufferInterface;
	class MeshInterface;
	class IndexBufferInterface;
	class VertexBufferInterface;
	class UniformBufferDx12;
	class FrameBufferDx12;
	class IndexBufferDx12;
	class VertexBufferDx12;
	class ShaderDx12;

	class DriverDx12 : public IKIGAI::RENDER::DriverInterface {
		struct State {
			std::shared_ptr<ShaderDx12> mShader;
			CullFace mCullFace = CullFace::NONE;
			TriangleOrientation mTriangleOrientation = TriangleOrientation::CW;
			std::optional<Depth> mDepth;
			std::optional<Blending> mBlend;
			std::optional<Stencil> mStencil;
			RasterizationMode mRasterization = RasterizationMode::FILL;
			std::shared_ptr<FrameBufferDx12> mFrameBuffer;

			[[nodiscard]] std::string getId() const;
		};
	public:
		static constexpr int DEFAULT_FB_SIZE = 2;
		static constexpr DXGI_FORMAT DefaultTextureColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		static constexpr DXGI_FORMAT DefaultDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

		static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

		DriverDx12();
		~DriverDx12() override;


		void onResize();
		void setPrimitiveMode(PrimitiveMode param) override;
		void setRasterization(RasterizationMode param) override;
		void setViewport(const Viewport& param) override;
		void resetViewport() override;
		void setScissor(const Scissor& param) override;
		void resetScissor() override;
		void setBlend(const Blending& param) override;
		void resetBlend() override;
		void setDepth(const Depth& param) override;
		void resetDepth() override;
		void setStencil(const Stencil& param) override;
		void resetStencil() override;
		void setCull(CullFace param) override;
		void setMSAA(bool value) override;
		bool get4xMsaaState() const;
		void setClearColor(const MATH::Vector4f& color) override;
		void setClearColor(float r, float g, float b, float a) override;
		void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) override;
		void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) override;
		void setShader(std::shared_ptr<ShaderInterface> shader) override;
		void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) override;
		void setTriangleOrientation(TriangleOrientation value) override {};

		void draw(uint32_t vertex_count, uint32_t vertex_offset, uint32_t instance_count) override;
		void drawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t instance_count) override;
		void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances);

		void submit() override;
		Microsoft::WRL::ComPtr<ID3D12Device> getDevice();
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> getCommandList();
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> getCommandQueue() { return mCommandQueue; }
		void destroyDeferred(Microsoft::WRL::ComPtr<ID3D12DeviceChild> object);
		std::vector<Microsoft::WRL::ComPtr<ID3D12DeviceChild>>& getDestroyDeferredObjects();
		void clear(bool clearColor, bool clearDepth, bool clearStencil) override;
		std::shared_ptr<FrameBufferDx12>& getDefaultFrameBuffer() { return mSwapchains[mCurrentWindowID].mDefaultFb[mFrameId]; }
		uint32_t getFrameBufferWidth() const {
			return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getTextures()[0]->getWidth() : mSwapchains.at(mCurrentWindowID).width;
		}

		uint32_t getFrameBufferHeight() const {
			return mCurrentState.mFrameBuffer ? mCurrentState.mFrameBuffer->getTextures()[0]->getHeight() : mSwapchains.at(mCurrentWindowID).height;
		}

		PixelFormat getFrameBufferFormat() const;

		void beginCommandList(ID3D12CommandAllocator* cmd_alloc, ID3D12GraphicsCommandList* cmd_list);
		void endCommandList(ID3D12CommandQueue* cmd_queue, ID3D12GraphicsCommandList* cmd_list, bool wait_for);

		void setTexture(size_t bind, std::shared_ptr<TextureInterface> data) override;
		void setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) override;
		void setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) override;

		void setTexture(const std::string& bind, std::shared_ptr<TextureInterface> data) override;
		void setUniformBuffer(const std::string& bind, std::shared_ptr<UniformBufferInterface> data) override;
		void setStorageBuffer(const std::string& bind, std::shared_ptr<StorageBufferInterface> data) override;

		std::shared_ptr<TextureInterface> createTexture(const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTextureAtlas(const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;
		std::shared_ptr<TextureInterface> createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr) override;

		std::shared_ptr<ShaderInterface> createShader(const std::string& vertexPath, const std::string& fragmentPath) override;
		std::shared_ptr<ShaderInterface> createShader(const ShaderResource& res, UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr) override;

		std::shared_ptr<ModelInterface> createModel(const std::string& path, UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr) override;

		std::shared_ptr<MaterialInterface> createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator = nullptr, MaterialDeleter deleter = nullptr) override;

		std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data, size_t size) override;
		std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride) override;
		std::shared_ptr<FrameBufferInterface> createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) override;

		CD3DX12_CPU_DESCRIPTOR_HANDLE& getDescriptorHeapCPUHandle() {
			return mDescriptorHeapCPUHandle;
		}
		CD3DX12_GPU_DESCRIPTOR_HANDLE& getDescriptorHeapGPUHandle() {
			return mDescriptorHeapGPUHandle;
		}
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& getDescriptorHeap() {
			return mDescriptorHeap;
		}
		unsigned getDescriptorIncSize() {
			return mDescriptorIncSize;
		}
	private:
		void begin() override;
		void end() override;
		
		MATH::Vector4f mClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
		float mDepthValue = 1.0f;
		float mStencilValue = 1.0f;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mDescriptorHeapCPUHandle{};
		CD3DX12_GPU_DESCRIPTOR_HANDLE mDescriptorHeapGPUHandle{};
		unsigned int mDescriptorIncSize = 0;

		std::vector<Microsoft::WRL::ComPtr<ID3D12DeviceChild>> mDestroyDeffered{};

		struct SwapchainContextDx12 {
			Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;
			std::array<std::shared_ptr<FrameBufferDx12>, DEFAULT_FB_SIZE> mDefaultFb;
			unsigned width = 0;
			unsigned height = 0;
		};

		std::unordered_map<unsigned int, SwapchainContextDx12> mSwapchains;
		unsigned int mCurrentWindowID = 0;

		std::map<size_t, std::shared_ptr<TextureDx12>> mTextures{};
		std::map<size_t, std::shared_ptr<UniformBufferDx12>> mUniformBuffers{};
		std::map<size_t, std::shared_ptr<StorageBufferDx12>> mStorageBuffers{};

		Microsoft::WRL::ComPtr<ID3D12Device> mDevice;

		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> mCommandList;

		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;

		Microsoft::WRL::ComPtr<IDXGIFactory6> mDxgiFactory;

		unsigned int mFrameId = 0;
		HANDLE mFenceEvent = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
		UINT64 mFenceValue{};

		State mCurrentState;
		std::map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mStates{};
		
		PrimitiveMode mPrimitiveMode = PrimitiveMode::TRIANGLES;
		std::optional<Viewport> mViewport;
		std::optional<Scissor> mScissor;
		std::shared_ptr<VertexBufferDx12> mVertexBuffer;
		std::shared_ptr<IndexBufferDx12> mIndexBuffer;

		unsigned m4xMsaaQuality = 0;
		bool m4xMsaaState = true;

		void init() override;
		void createCommandList();
		void createDescriptorHeaps();
		void createSwapChain(unsigned int windowID, unsigned width, unsigned height);
		void logAdapters();
		void logOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
		Microsoft::WRL::ComPtr<ID3D12PipelineState> createNewState(const State& state);
		void applyState();
		void createDefaultFrameBuffer(unsigned int windowID, unsigned width, unsigned height);
		void logAdapterOutputs(IDXGIAdapter* adapter);
		void clearForDestroy();
		void wait();

		FrameInfo mFrameInfo;
	public:
		void setViewport(const ShaderInterface& shader, float x, float y, float w, float h) override{};
		void setScissor(const ShaderInterface& shader, int x, int y, unsigned w, unsigned h) override{};
		void drawIndexed(std::shared_ptr<ShaderInterface> shader, size_t indexCount) override{};
		void draw(std::shared_ptr<ShaderInterface> shader, size_t vertexCount) override{};
		void cleanup() override{};
	};

}

#endif

