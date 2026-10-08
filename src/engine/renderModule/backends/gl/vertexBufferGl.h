#pragma once

#ifdef OPENGL_BACKEND
#include <vector>
#include "../interface/vertexBufferInterface.h"
#include <cstdint>

namespace IKIGAI::RENDER
{
	class VertexBufferGl : public VertexBufferInterface {
	public:
		using Id = unsigned;
		enum class UsageType : uint8_t {
			STREAM_DRAW = 0,
			STREAM_READ,
			STREAM_COPY,
			STATIC_DRAW,
			STATIC_READ,
			STATIC_COPY,
			DYNAMIC_DRAW,
			DYNAMIC_READ,
			DYNAMIC_COPY,
		};
	public:
		template <class T>
		VertexBufferGl(const std::vector<T>& vertices, UsageType type = UsageType::STATIC_DRAW): VertexBufferGl(static_cast<const void*>(vertices.data()), vertices.size(), sizeof(T), type) {}
		VertexBufferGl(const void* data, size_t sz, size_t stride, UsageType type);
		~VertexBufferGl() override;
		void bind() override;
		void unbind() override;
		void setData(const void* data, size_t sz, size_t stride) override;
		[[nodiscard]] Id getID() const;

		void bindAttribute(unsigned index, int size, unsigned type, bool normalized, int stride, const void* pointer);
		void bindIAttribute(unsigned index, int size, unsigned type, int stride, const void* pointer);
		void setSubData(long long offset, long long size, const void* data);

	private:
		Id mId = 0;
		UsageType mType = UsageType::STATIC_DRAW;

	};


	class VertexArray {
	public:
		using Id = unsigned;
		enum class AttributeType {
			BYTE = 0,
			UNISGNED_BYTE,
			SHORT,
			UNSIGNED_SHORT,
			INT,
			UNSIGNED_INT,
			FLOAT,
			DOUBLE
		};
	public:
		VertexArray();
		~VertexArray();
		void bind() const;
		void unbind() const;
		[[nodiscard]] Id getID() const;
		void bindAttribute(unsigned int attribute, VertexBufferGl& vertexBuffer, AttributeType type, unsigned int count, unsigned int stride, intptr_t offset) const;

	private:
		Id mId = 0;
	};
}

#endif
