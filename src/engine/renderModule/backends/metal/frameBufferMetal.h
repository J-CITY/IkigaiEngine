#pragma once
#ifdef METAL_BACKEND

#include "../interface/frameBufferInterface.h"
#include <vector>
#include <memory>

namespace IKIGAI::RENDER {

    class FrameBufferMetal : public FrameBufferInterface {
    public:
        FrameBufferMetal(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth);
        virtual ~FrameBufferMetal() override = default;

        const std::vector<std::shared_ptr<TextureInterface>>& getTextures() const override { return mTextures; }
        const std::shared_ptr<TextureInterface>& getDepth() const override { return mDepth; }

    private:
        std::vector<std::shared_ptr<TextureInterface>> mTextures;
        std::shared_ptr<TextureInterface> mDepth;
    };

}

#endif
