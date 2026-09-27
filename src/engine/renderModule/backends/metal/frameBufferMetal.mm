#ifdef METAL_BACKEND
#include "frameBufferMetal.h"
#include "../interface/textureInterface.h"

namespace IKIGAI::RENDER {

    FrameBufferMetal::FrameBufferMetal(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth) 
        : mTextures(textures), mDepth(depth) 
    {
        static unsigned ID = 1; // start from 1, 0 is usually reserved/invalid or default backbuffer
        mId = ID++;
        
        // Find dimensions from textures
        if (!mTextures.empty() && mTextures[0]) {
            mWidth = mTextures[0]->getWidth();
            mHeight = mTextures[0]->getHeight();
        } else if (mDepth) {
            mWidth = mDepth->getWidth();
            mHeight = mDepth->getHeight();
        }
    }

}
#endif
