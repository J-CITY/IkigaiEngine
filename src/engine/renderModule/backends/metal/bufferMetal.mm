#ifdef METAL_BACKEND
#include "bufferMetal.h"
#include <cstring>

namespace IKIGAI::RENDER {

    // --- UniformBufferMetal ---
    UniformBufferMetal::UniformBufferMetal(id<MTLDevice> device, size_t sz) : UniformBufferInterface(sz) {
        if (sz > 0) {
            mBuffer = [device newBufferWithLength:sz options:MTLResourceStorageModeShared];
        }
    }

    void UniformBufferMetal::setData(const void* data, size_t sz, size_t offset) {
        if (mBuffer && data && sz > 0 && offset + sz <= mSizeByte) {
            std::memcpy((uint8_t*)[mBuffer contents] + offset, data, sz);
        }
    }

    // --- StorageBufferMetal ---
    StorageBufferMetal::StorageBufferMetal(id<MTLDevice> device, size_t sz, size_t stride) : StorageBufferInterface(sz, stride) {
        if (mSizeByte > 0) {
            mBuffer = [device newBufferWithLength:mSizeByte options:MTLResourceStorageModeShared];
        }
    }

    void StorageBufferMetal::setData(const void* data, size_t sz, size_t stride) {
        mSize = sz;
        mStride = stride;
        mSizeByte = sz * stride;
        
        if (mBuffer && [mBuffer length] < mSizeByte) {
            id<MTLDevice> device = [mBuffer device];
            mBuffer = [device newBufferWithLength:mSizeByte options:MTLResourceStorageModeShared];
        }
        
        if (mBuffer && data && mSizeByte > 0) {
            std::memcpy([mBuffer contents], data, mSizeByte);
        }
    }

    void StorageBufferMetal::setSubData(const void* data, size_t sz, size_t offset) {
        if (mBuffer && data && sz > 0 && offset + sz <= mSizeByte) {
            std::memcpy((uint8_t*)[mBuffer contents] + offset, data, sz);
        }
    }

    // --- VertexBufferMetal ---
    VertexBufferMetal::VertexBufferMetal(id<MTLDevice> device, size_t sz, size_t stride) : VertexBufferInterface(sz, stride) {
        if (mSizeByte > 0) {
            mBuffer = [device newBufferWithLength:mSizeByte options:MTLResourceStorageModeShared];
        }
    }

    void VertexBufferMetal::setData(const void* data, size_t sz, size_t stride) {
        mSize = sz;
        mStride = stride;
        mSizeByte = sz * stride;
        
        if (mBuffer && [mBuffer length] < mSizeByte) {
            id<MTLDevice> device = [mBuffer device];
            mBuffer = [device newBufferWithLength:mSizeByte options:MTLResourceStorageModeShared];
        }
        
        if (mBuffer && data && mSizeByte > 0) {
            std::memcpy([mBuffer contents], data, mSizeByte);
        }
    }

    // --- IndexBufferMetal ---
    IndexBufferMetal::IndexBufferMetal(id<MTLDevice> device, size_t sz, size_t stride) : IndexBufferInterface(sz, stride) {
        if (mSizeByte > 0) {
            mBuffer = [device newBufferWithLength:mSizeByte options:MTLResourceStorageModeShared];
        }
    }

    void IndexBufferMetal::setData(const void* data, size_t sz, size_t stride) {
        mSize = sz;
        mStride = stride;
        mSizeByte = sz * stride;
        
        if (mBuffer && [mBuffer length] < mSizeByte) {
            id<MTLDevice> device = [mBuffer device];
            mBuffer = [device newBufferWithLength:mSizeByte options:MTLResourceStorageModeShared];
        }
        
        if (mBuffer && data && mSizeByte > 0) {
            std::memcpy([mBuffer contents], data, mSizeByte);
        }
    }

}
#endif
