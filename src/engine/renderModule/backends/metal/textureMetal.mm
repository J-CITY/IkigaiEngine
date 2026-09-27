#ifdef METAL_BACKEND
#include "textureMetal.h"

namespace IKIGAI::RENDER {

    TextureMetal::TextureMetal(id<MTLDevice> device, const std::string& path, bool generateMipmap) 
        : mDevice(device) 
    {
        mPath = path;
        mUseMipMap = generateMipmap;
        // Stub: load texture using MTKTextureLoader or stb_image and create MTLTexture
        createSampler();
    }

    TextureMetal::TextureMetal(id<MTLDevice> device, const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData)
        : mDevice(device) 
    {
        // Stub: initialize from memory bytes
        createSampler();
    }

    TextureMetal::TextureMetal(id<MTLDevice> device, const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap)
        : mDevice(device)
    {
        mPath = name;
        mUseMipMap = generateMipmap;
        // Stub: create from byte vector
        createSampler();
    }

    void TextureMetal::recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) {
        // Stub: reload from fileData
    }

    void TextureMetal::createSampler() {
        if (!mDevice) return;
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
        samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        mSampler = [mDevice newSamplerStateWithDescriptor:samplerDesc];
    }

}
#endif
