#ifdef METAL_BACKEND

#include "driverMetal.h"

#include "resourceModule/serviceManager.h"
#include "windowModule/window/window.h"
#include "utilsModule/imguiHelper/imguiBackend/imguiBackend.h"
#import <QuartzCore/CAMetalLayer.h>

#include "bufferMetal.h"
#include "textureMetal.h"
#include "frameBufferMetal.h"
#include "modelMetal.h"
#include "materialMetal.h"

namespace IKIGAI::RENDER {

    std::unique_ptr<DriverInterface> CreateDriverMetal() {
        return std::make_unique<DriverMetal>();
    }

    std::string DriverMetal::State::getName() {
        std::string name = mShader ? std::to_string(mShader->getId()) : "0";
        name += "_";
        name += mFrameBuffer ? std::to_string(mFrameBuffer->getId()) : "0";
        name += "_";
        if (mBlending) {
            name += std::to_string((int)mBlending->mColorSrc) + "_";
            name += std::to_string((int)mBlending->mColorDst) + "_";
            // More blending states can be appended here if needed
        } else {
            name += "noblend";
        }
        return name;
    }
    
    void DriverMetal::ensurePipelineState() {
        if (!mCurrentState.mShader || !mCurrentEncoder) return;
        
        std::string stateName = mCurrentState.getName();
        if (mStates.find(stateName) == mStates.end()) {
            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = mCurrentState.mShader->getVertexFunction();
            desc.fragmentFunction = mCurrentState.mShader->getFragmentFunction();
            
            // Assume single color attachment for now. Real implementation checks FrameBuffer format.
            desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            
            if (mCurrentState.mBlending) {
                desc.colorAttachments[0].blendingEnabled = YES;
                // Simplified mapping. You'll need to map IKIGAI BlendMode to MTLBlendFactor
                // desc.colorAttachments[0].sourceRGBBlendFactor = ...
                // desc.colorAttachments[0].destinationRGBBlendFactor = ...
            }
            
            NSError* err = nil;
            id<MTLRenderPipelineState> pipelineState = [mDevice newRenderPipelineStateWithDescriptor:desc error:&err];
            if (err) {
                // Handle error
            }
            if (pipelineState) {
                mStates[stateName] = pipelineState;
            }
        }
        
        if (mStates.count(stateName)) {
            [mCurrentEncoder setRenderPipelineState:mStates[stateName]];
        }
    }

    DriverMetal::DriverMetal() {
        // init();
    }

    DriverMetal::~DriverMetal() {
        cleanup();
    }

    void DriverMetal::init() {
        mDevice = MTLCreateSystemDefaultDevice();
        mCommandQueue = [mDevice newCommandQueue];
        
        auto& win = RESOURCES::ServiceManager::Get<WINDOW::Window>();
        CAMetalLayer* layer = (__bridge CAMetalLayer*)win.getMetalLayer();
        layer.device = mDevice;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        
        // Save the layer in a member variable (requires adding to header)
        // Or we can just keep a reference to win
    }

    void DriverMetal::begin() {
        auto& win = RESOURCES::ServiceManager::Get<WINDOW::Window>();
        CAMetalLayer* layer = (__bridge CAMetalLayer*)win.getMetalLayer();
        mCurrentDrawable = [layer nextDrawable];
        if (!mCurrentDrawable) return;

        MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        passDescriptor.colorAttachments[0].texture = mCurrentDrawable.texture;
        passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(mClearColor.x, mClearColor.y, mClearColor.z, mClearColor.w);
        passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

        mCurrentCommandBuffer = [mCommandQueue commandBuffer];
        mCurrentEncoder = [mCurrentCommandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
    }

    void DriverMetal::end() {
        if (auto* imgui = IKIGAI::IMGUI::Get()) {
            imgui->renderDrawData();
        }
        if (mCurrentEncoder) {
            [mCurrentEncoder endEncoding];
            mCurrentEncoder = nil;
        }
        
        if (mCurrentCommandBuffer && mCurrentDrawable) {
            [mCurrentCommandBuffer presentDrawable:mCurrentDrawable];
            [mCurrentCommandBuffer commit];
        }
        
        mCurrentCommandBuffer = nil;
        mCurrentDrawable = nil;
    }

    void DriverMetal::submit() {
        // Build state and submit draw call to encoder
    }

    void DriverMetal::cleanup() {
        // Release resources if not using ARC
    }

    void DriverMetal::setPrimitiveMode(PrimitiveMode topology) {}
    void DriverMetal::setRasterization(RasterizationMode mode) {}
    void DriverMetal::setViewport(const Viewport& viewport) {}
    void DriverMetal::resetViewport() {}
    void DriverMetal::setScissor(const Scissor& scissor) {}
    void DriverMetal::resetScissor() {}
    void DriverMetal::setShader(std::shared_ptr<ShaderInterface> shader) {
        if (!shader || !mCurrentEncoder) return;
        auto metalShader = std::static_pointer_cast<ShaderMetal>(shader);
        mCurrentState.mShader = metalShader;
    }
    
    void DriverMetal::setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) {
        if (!buffer || !mCurrentEncoder) return;
        auto buf = std::static_pointer_cast<VertexBufferMetal>(buffer);
        [mCurrentEncoder setVertexBuffer:buf->getBuffer() offset:0 atIndex:0]; // Assuming index 0 for vertices
    }
    
    void DriverMetal::setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) {
        if (!buffer || !mCurrentEncoder) return;
        auto buf = std::static_pointer_cast<IndexBufferMetal>(buffer);
        mCurrentIndexBuffer = buf->getBuffer();
    }
    void DriverMetal::setBlending(const Blending& value) {
        mCurrentState.mBlending = value;
    }
    void DriverMetal::resetBlending() {
        mCurrentState.mBlending.reset();
    }
    void DriverMetal::setDepth(const Depth& depth) {}
    void DriverMetal::resetDepth() {}
    void DriverMetal::setStencil(const Stencil& stencil) {}
    void DriverMetal::resetStencil() {}
    void DriverMetal::setCull(CullFace cull_mode) {}
    void DriverMetal::setTriangleOrientation(TriangleOrientation value) {}
    void DriverMetal::clear(bool clearColor, bool clearDepth, bool clearStencil) {}
    
    void DriverMetal::setClearColor(const MATH::Vector4f& color) {
        mClearColor = color;
    }
    
    void DriverMetal::setClearColor(float r, float g, float b, float a) {
        mClearColor = {r, g, b, a};
    }
    
    void DriverMetal::draw(uint32_t count, uint32_t offset, uint32_t instance) {
        if (!mCurrentEncoder) return;
        ensurePipelineState();
        [mCurrentEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:offset vertexCount:count instanceCount:instance];
    }
    void DriverMetal::drawIndexed(uint32_t count, uint32_t offset, uint32_t instance) {
        if (!mCurrentEncoder || !mCurrentIndexBuffer) return;
        ensurePipelineState();
        
        NSUInteger byteOffset = offset * sizeof(uint32_t); 
        [mCurrentEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle 
                                    indexCount:count 
                                     indexType:MTLIndexTypeUInt32 
                                   indexBuffer:mCurrentIndexBuffer 
                             indexBufferOffset:byteOffset 
                                 instanceCount:instance];
    }
    
    void DriverMetal::setPushConstant(ShaderType stage, uint32_t offset, uint32_t size, const void* data) {
        if (!mCurrentEncoder) return;
        
        // Note: The index (e.g. 1) must match where SPIRV-Cross maps the push constants. 
        // SPIRV-Cross usually puts push constants at an unassigned buffer index (often 0 or the next free slot).
        // If VertexBuffer is at index 0, Push Constant might be mapped to index 1 or 25 by SPIRV-Cross.
        const int PUSH_CONSTANT_BUFFER_INDEX = 1; 

        if (stage == ShaderType::VERTEX) {
            [mCurrentEncoder setVertexBytes:data length:size atIndex:PUSH_CONSTANT_BUFFER_INDEX];
        } else if (stage == ShaderType::FRAGMENT) {
            [mCurrentEncoder setFragmentBytes:data length:size atIndex:PUSH_CONSTANT_BUFFER_INDEX];
        }
    }

    void DriverMetal::setTexture(size_t bind, std::shared_ptr<TextureInterface> data) {
        if (!data || !mCurrentEncoder) return;
        auto tex = std::static_pointer_cast<TextureMetal>(data);
        [mCurrentEncoder setFragmentTexture:tex->getTexture() atIndex:bind];
        [mCurrentEncoder setFragmentSamplerState:tex->getSampler() atIndex:bind];
    }
    void DriverMetal::setUniformBuffer(size_t bind, std::shared_ptr<UniformBufferInterface> data) {
        if (!data || !mCurrentEncoder) return;
        auto buf = std::static_pointer_cast<UniformBufferMetal>(data);
        [mCurrentEncoder setVertexBuffer:buf->getBuffer() offset:0 atIndex:bind];
        [mCurrentEncoder setFragmentBuffer:buf->getBuffer() offset:0 atIndex:bind];
    }
    void DriverMetal::setStorageBuffer(size_t bind, std::shared_ptr<StorageBufferInterface> data) {
        if (!data || !mCurrentEncoder) return;
        auto buf = std::static_pointer_cast<StorageBufferMetal>(data);
        [mCurrentEncoder setVertexBuffer:buf->getBuffer() offset:0 atIndex:bind];
        [mCurrentEncoder setFragmentBuffer:buf->getBuffer() offset:0 atIndex:bind];
    }
    void DriverMetal::setTexture(const std::string& name, std::shared_ptr<TextureInterface> data) {}
    void DriverMetal::setUniformBuffer(const std::string& name, std::shared_ptr<UniformBufferInterface> data) {}
    void DriverMetal::setStorageBuffer(const std::string& name, std::shared_ptr<StorageBufferInterface> data) {}
    void DriverMetal::setMSAA(bool value) {}

    std::shared_ptr<UniformBufferInterface> DriverMetal::createUniformBuffer(const void* data, size_t size) { 
        auto buffer = std::make_shared<UniformBufferMetal>(mDevice, size);
        if (data) buffer->setData(data, size);
        return buffer;
    }
    std::shared_ptr<StorageBufferInterface> DriverMetal::createStorageBuffer(const void* data, size_t size, size_t stride) { 
        auto buffer = std::make_shared<StorageBufferMetal>(mDevice, size, stride);
        if (data) buffer->setData(data, size, stride);
        return buffer;
    }
    std::shared_ptr<TextureInterface> DriverMetal::createTexture(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) { 
        return std::make_shared<TextureMetal>(mDevice, path, generateMipmap);
    }
    std::shared_ptr<TextureInterface> DriverMetal::createTextureAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) { 
        return std::make_shared<TextureAtlasMetal>(mDevice, path, generateMipmap);
    }
    std::shared_ptr<TextureInterface> DriverMetal::createTexture(const TextureResource& res, UTILS::IAllocator* allocator, TextureDeleter deleter) { 
        std::vector<std::vector<uint8_t>> emptyData;
        return std::make_shared<TextureMetal>(mDevice, res, emptyData);
    }
    std::shared_ptr<TextureInterface> DriverMetal::createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) { 
        return std::make_shared<TextureMetal>(mDevice, res, fileData);
    }
    std::shared_ptr<TextureInterface> DriverMetal::createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator, TextureDeleter deleter) { 
        return std::make_shared<TextureAtlasMetal>(mDevice, res, fileData);
    }
    std::shared_ptr<TextureInterface> DriverMetal::createTexture(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, TextureDeleter deleter) { 
        return std::make_shared<TextureMetal>(mDevice, name, data, generateMipmap);
    }
    
    std::shared_ptr<ShaderInterface> DriverMetal::createShader(const std::string& vertexPath, const std::string& fragmentPath) { 
        return std::make_shared<ShaderMetal>(mDevice, vertexPath, fragmentPath);
    }
    std::shared_ptr<ShaderInterface> DriverMetal::createShader(const ShaderResource& res, UTILS::IAllocator* allocator, ShaderDeleter deleter) { 
        // Need to load from ShaderResource. Assuming vertex and fragment paths exist in resource map.
        auto vertPath = res.paths.at(ShaderResource::toStr.at(ShaderType::VERTEX));
        auto fragPath = res.paths.at(ShaderResource::toStr.at(ShaderType::FRAGMENT));
        // Using AllocateShader helper or std::make_shared
        return std::make_shared<ShaderMetal>(mDevice, vertPath, fragPath);
    }
    
    std::shared_ptr<ModelInterface> DriverMetal::createModel(const std::string& path, UTILS::IAllocator* allocator, ModelDeleter deleter) { 
        return std::make_shared<ModelMetal>(path);
    }
    std::shared_ptr<MaterialInterface> DriverMetal::createMaterial(const MaterialResource& res, UTILS::IAllocator* allocator, MaterialDeleter deleter) { 
        return std::make_shared<MaterialMetal>(res);
    }
    
    std::shared_ptr<FrameBufferInterface> DriverMetal::createFrameBuffer(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) { 
        return std::make_shared<FrameBufferMetal>(textures, depth);
    }
    void DriverMetal::setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) {
        mCurrentState.mFrameBuffer = frameBuffer;
        // In Metal, changing framebuffer means ending the current MTLRenderCommandEncoder 
        // and starting a new one with a new MTLRenderPassDescriptor.
    }
    void DriverMetal::resetFrameBuffer() {
        mCurrentState.mFrameBuffer.reset();
        // Return to window's backbuffer
    }
    void DriverMetal::draw(const MeshInterface& mesh, PrimitiveMode primitive, uint32_t instances) {}

}

#endif
