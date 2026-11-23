#ifdef METAL_BACKEND
#include "bufferMetal.h"
#include "driverMetal.h"
#include "gpuMemoryMetal.h"

#include <cstring>
#include <limits>
#include <stdexcept>

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
	StorageBufferMetal::StorageBufferMetal(size_t sz, size_t stride, BufferUsage usage, const void* data)
		: StorageBufferInterface(0, stride)
		, mUsage(usage | BufferUsage::STORAGE | BufferUsage::TRANSFER_DST) {
		setData(data, sz, stride);
	}

	void StorageBufferMetal::setData(const void* data, size_t sz, size_t stride) {
		requireOwnerThread();
		if (sz && (!stride || sz > std::numeric_limits<size_t>::max() / stride)) {
			throw std::invalid_argument("Invalid or overflowing Metal storage buffer size");
		}
		const size_t bytes = sz * stride;
		id<MTLDevice> device = DeviceMetal::Get();
		if (!device || bytes > device.maxBufferLength || bytes > std::numeric_limits<size_t>::max() - 3) {
			throw std::invalid_argument("Metal storage buffer exceeds the device allocation limit");
		}
		// Keep the logical size exact. Pad the native allocation for word-aligned blit readbacks.
		if (!mBuffer || mBuffer.length < bytes) {
			const size_t capacity = std::max<size_t>((bytes + 3) & ~size_t{3}, 16);
			id<MTLBuffer> buffer = AllocateBufferMetal(device, capacity,
				MTLResourceStorageModeShared | MTLResourceHazardTrackingModeTracked, PROFILER::GpuResourceKind::StorageBuffer);
			if (!buffer) {
				throw std::runtime_error("Metal storage buffer allocation failed");
			}
			if (data && bytes) {
				std::memcpy(buffer.contents, data, bytes);
			}
			mBuffer = buffer;
		} else if (data && bytes) {
			auto& driver = dynamic_cast<DriverMetal&>(*DriverInterface::Get());
			driver.uploadStorageBuffer(mBuffer, data, bytes, 0);
		}

		mSize = sz;
		mStride = stride;
		mSizeByte = bytes;
	}

	void StorageBufferMetal::setSubData(const void* data, size_t sz, size_t offset) {
		requireOwnerThread();
		if (!sz) {
			return;
		}
		if (!data || offset > mSizeByte || sz > mSizeByte - offset) {
			throw std::invalid_argument("Metal storage buffer update exceeds its logical range");
		}
		auto& driver = dynamic_cast<DriverMetal&>(*DriverInterface::Get());
		driver.uploadStorageBuffer(mBuffer, data, sz, offset);
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
