#ifdef METAL_BACKEND
#include "bufferMetal.h"
#include "gpuMemoryMetal.h"

#include <cstring>

#include "deviceMetal.h"

namespace IKIGAI::RENDER {

    static id<MTLBuffer> MakeSharedBuffer(size_t bytes, PROFILER::GpuResourceKind kind) {
        if (bytes < 16) {
            bytes = 16;
        }
        id<MTLDevice> device = DeviceMetal::Get();
        if (!device) {
            return nil;
        }
        return AllocateBufferMetal(device, bytes, MTLResourceStorageModeShared, kind);
    }

    // --- UniformBufferMetal ---
    UniformBufferMetal::UniformBufferMetal(size_t sz) : UniformBufferInterface(sz) {
        if (sz > 0) {
            // MSL rounds std140 blocks up to the struct alignment (16 when the block contains a vec4).
            const size_t aligned = (sz + 15u) & ~size_t{15};
            mSizeByte = aligned;
            mBuffer = MakeSharedBuffer(aligned, PROFILER::GpuResourceKind::UniformBuffer);
        } else {
            mSizeByte = 16;
            mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::UniformBuffer);
        }
    }

    void UniformBufferMetal::setData(const void* data, size_t sz, size_t offset) {
		requireOwnerThread();
		if (mBuffer && data && sz > 0 && offset + sz <= mSizeByte) {
			std::memcpy((uint8_t*)[mBuffer contents] + offset, data, sz);
		}
	}

	// --- StorageBufferMetal ---
	StorageBufferMetal::StorageBufferMetal(size_t sz, size_t stride)
		: StorageBufferInterface(sz, stride) {
		mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::StorageBuffer);
	}

	void StorageBufferMetal::setData(const void* data, size_t sz, size_t stride) {
		requireOwnerThread();
		mSize = sz;
		mStride = stride;
		mSizeByte = sz * stride;

		if (!mBuffer || [mBuffer length] < mSizeByte) {
			mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::StorageBuffer);
		}

		if (mBuffer && data && mSizeByte > 0) {
			std::memcpy([mBuffer contents], data, mSizeByte);
		}
	}

	void StorageBufferMetal::setSubData(const void* data, size_t sz, size_t offset) {
		requireOwnerThread();
		if (mBuffer && data && sz > 0 && offset + sz <= mSizeByte) {
			std::memcpy((uint8_t*)[mBuffer contents] + offset, data, sz);
		}
	}

	// --- VertexBufferMetal ---
	VertexBufferMetal::VertexBufferMetal(size_t sz, size_t stride)
		: VertexBufferInterface(sz, stride) {
		mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::VertexBuffer);
	}

	void VertexBufferMetal::setData(const void* data, size_t sz, size_t stride) {
		requireOwnerThread();
		mSize = sz;
		mStride = stride;
		mSizeByte = sz * stride;

		if (!mBuffer || [mBuffer length] < mSizeByte) {
			mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::VertexBuffer);
		}

		if (mBuffer && data && mSizeByte > 0) {
			std::memcpy([mBuffer contents], data, mSizeByte);
		}
	}

	// --- IndexBufferMetal ---
    IndexBufferMetal::IndexBufferMetal(size_t sz, size_t stride) : IndexBufferInterface(sz, stride) {
        mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::IndexBuffer);
    }

    void IndexBufferMetal::setData(const void* data, size_t sz, size_t stride) {
		requireOwnerThread();
		mSize = sz;
		mStride = stride;
		mSizeByte = sz * stride;

		if (!mBuffer || [mBuffer length] < mSizeByte) {
			mBuffer = MakeSharedBuffer(mSizeByte, PROFILER::GpuResourceKind::IndexBuffer);
		}

		if (mBuffer && data && mSizeByte > 0) {
			std::memcpy([mBuffer contents], data, mSizeByte);
		}
	}

}
#endif
