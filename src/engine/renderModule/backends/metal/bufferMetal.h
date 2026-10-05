#pragma once
#ifdef METAL_BACKEND

#include "../interface/uniformBufferInterface.h"
#include "../interface/storageBufferInterface.h"
#include "../interface/vertexBufferInterface.h"
#include "../interface/indexBufferInterface.h"

#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

    class UniformBufferMetal : public UniformBufferInterface {
    public:
        explicit UniformBufferMetal(size_t sz);
        virtual ~UniformBufferMetal() override = default;
        
        void setData(const void* data, size_t sz, size_t offset = 0) override;
        id<MTLBuffer> getBuffer() const { return mBuffer; }

    private:
        id<MTLBuffer> mBuffer;
    };

    class StorageBufferMetal : public StorageBufferInterface {
    public:
        StorageBufferMetal(size_t sz, size_t stride);
        virtual ~StorageBufferMetal() override = default;
        
        void bind() override {}
        void unbind() override {}
        
        void setData(const void* data, size_t sz, size_t stride) override;
        void setSubData(const void* data, size_t sz, size_t offset) override;
        
        id<MTLBuffer> getBuffer() const { return mBuffer; }

    private:
        id<MTLBuffer> mBuffer;
    };

    class VertexBufferMetal : public VertexBufferInterface {
    public:
        VertexBufferMetal(size_t sz, size_t stride);
        virtual ~VertexBufferMetal() override = default;
        
        void bind() override {}
        void unbind() override {}
        void setData(const void* data, size_t sz, size_t stride) override;
        
        id<MTLBuffer> getBuffer() const { return mBuffer; }

    private:
        id<MTLBuffer> mBuffer;
    };

    class IndexBufferMetal : public IndexBufferInterface {
    public:
        IndexBufferMetal(size_t sz, size_t stride);
        virtual ~IndexBufferMetal() override = default;
        
        void bind() override {}
        void unbind() override {}
        void setData(const void* data, size_t sz, size_t stride) override;
        
        id<MTLBuffer> getBuffer() const { return mBuffer; }

    private:
        id<MTLBuffer> mBuffer;
    };

}

#endif
