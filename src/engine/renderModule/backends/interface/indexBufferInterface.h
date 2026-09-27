#pragma once
#include <cstddef>
#include <vector>

namespace IKIGAI::RENDER {
	class IndexBufferInterface {
	protected:
		size_t mSize = 0;
		size_t mStride = 0;
		size_t mSizeByte = 0;
	
	public:
		IndexBufferInterface(size_t sz, size_t stride): mSize(sz), mStride(stride), mSizeByte(sz * stride) {}
		virtual ~IndexBufferInterface() = default;
		virtual void bind() = 0;
		virtual void unbind() = 0;
		inline size_t getSize() const { return mSize; };
		inline size_t getStride() const { return mStride; };
		inline size_t getSizeByte() const { return mSizeByte; };
	
		// TODO: check T is integer
		virtual void setData(const void *data, size_t sz, size_t stride) = 0;
		template <class T> void setData(const std::vector<T> &data) {
			setData(data.data(), data.size(), sizeof(T));
		}
	};
} // namespace IKIGAI::RENDER
