#pragma once
#ifdef METAL_BACKEND

#include "../interface/driverInterface.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "shaderMetal.h"

namespace IKIGAI::RENDER {

    class DriverMetal : public DriverInterface {
    public:
        DriverMetal();
        virtual ~DriverMetal() override;

        void init() override;
        void begin() override;
        void end() override;
        void submit() override;
        void cleanup() override;

        void setPrimitiveMode(PrimitiveMode topology) override;
        void setRasterization(RasterizationMode mode) override;
        void setViewport(const Viewport& viewport) override;
        void resetViewport() override;
        void setScissor(const Scissor& scissor) override;
        void resetScissor() override;
        
        void setShader(std::shared_ptr<ShaderInterface> shader) override;
        void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) override;
        void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) override;
        
        void setBlending(const Blending& value) override;
        void resetBlending() override;
        void setDepth(const Depth& depth) override;
        void resetDepth() override;
        void setStencil(const Stencil& stencil) override;
        void resetStencil() override;
        void setCull(CullFace cull_mode) override;
        void setTriangleOrientation(TriangleOrientation value) override;
        
        void clear(bool clearColor, bool clearDepth, bool clearStencil) override;
        void setClearColor(const MATH::Vector4f& color) override;
        void setClearColor(float r, float g, float b, float a) override;
        
        void draw(uint32_t count, uint32_t offset, uint32_t instance) override;
        void drawIndexed(uint32_t count, uint32_t offset, uint32_t instance) override;

        void setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) override;

        void setTexture(size_t bind, std::shared_ptr<TextureInterface> data) override;
        void setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) override;
        void setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) override;
        void setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) override;
        void setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) override;
        void setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) override;

        void setMSAA(bool value) override;

        std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data, size_t size) override;
        std::shared_ptr<StorageBufferInterface> createStorageBuffer(const void* data, size_t size, size_t stride) override;
        std::shared_ptr<TextureInterface> createTexture(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
        std::shared_ptr<TextureInterface> createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
        std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
        std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
        std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
        std::shared_ptr<TextureInterface> createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) override;
        
        std::shared_ptr<ShaderInterface> createShader(const std::string& vertexPath, const std::string& fragmentPath) override;
        std::shared_ptr<ShaderInterface> createShader(const ShaderResource& res, UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr) override;
        
        std::shared_ptr<ModelInterface> createModel(const std::string& path, UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr) override;
        std::shared_ptr<MaterialInterface> createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator = nullptr, MaterialDeleter deleter = nullptr) override;
        
        std::shared_ptr<FrameBufferInterface> createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) override;
        void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) override;
        void resetFrameBuffer() override;

        void draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) override;

        // Metal specific members
        id<MTLDevice> getDevice() const { return mDevice; }
        id<MTLCommandQueue> getCommandQueue() const { return mCommandQueue; }

        // Pipeline cache mechanism
        struct State {
            std::shared_ptr<ShaderMetal> mShader;
            std::shared_ptr<FrameBufferInterface> mFrameBuffer;
            std::optional<Blending> mBlending;

            std::string getName();
        };

        State mCurrentState;
        std::map<std::string, id<MTLRenderPipelineState>> mStates;

        void ensurePipelineState();
        
    private:
        id<MTLDevice> mDevice;
        id<MTLCommandQueue> mCommandQueue;
        id<MTLCommandBuffer> mCurrentCommandBuffer;
        id<MTLRenderCommandEncoder> mCurrentEncoder;
        id<CAMetalDrawable> mCurrentDrawable;
        id<MTLBuffer> mCurrentIndexBuffer;
        
        MATH::Vector4f mClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    };

} // namespace IKIGAI::RENDER

#endif
