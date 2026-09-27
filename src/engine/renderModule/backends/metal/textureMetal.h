#pragma once
#ifdef METAL_BACKEND

#include "../interface/textureInterface.h"
#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

    class TextureMetal : public TextureInterface {
    public:
        TextureMetal(id<MTLDevice> device, const std::string& path, bool generateMipmap);
        TextureMetal(id<MTLDevice> device, const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData);
        TextureMetal(id<MTLDevice> device, const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap);
        // Add more constructors for depth textures etc. if needed
        
        virtual ~TextureMetal() override = default;
        
        void* getImguiId() override { return (__bridge void*)mTexture; }
        
        void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

        id<MTLTexture> getTexture() const { return mTexture; }
        id<MTLSamplerState> getSampler() const { return mSampler; }

    private:
        id<MTLDevice> mDevice;
        id<MTLTexture> mTexture;
        id<MTLSamplerState> mSampler;
        
        void createSampler();
    };

    class TextureAtlasMetal : public TextureMetal {
    public:
        TextureAtlasMetal(id<MTLDevice> device, const std::string& path, bool generateMipmap) : TextureMetal(device, path, generateMipmap) {}
        TextureAtlasMetal(id<MTLDevice> device, const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData) : TextureMetal(device, res, fileData) {}
    };

}
#endif
