#pragma once
#ifdef OPENGL_BACKEND
#include <vector>
#include "../interface/indexBufferInterface.h"
#include <cstdint>

namespace IKIGAI::RENDER
{
	class IndexBufferGl : public IndexBufferInterface {
	public:
		using Id = unsigned;
		enum class UsageType : uint8_t {
			STREAM_DRAW,
//#ifndef USING_GLES
			STREAM_READ,
			STREAM_COPY,
//#endif
			STATIC_DRAW,
//#ifndef USING_GLES
			STATIC_READ,
			STATIC_COPY,
//#endif
			DYNAMIC_DRAW,
//#ifndef USING_GLES
			DYNAMIC_READ,
			DYNAMIC_COPY,
//#endif
		};
	public:
		template<class T>
		IndexBufferGl(const std::vector<T>& vertices, UsageType type = UsageType::STATIC_DRAW) : IndexBufferGl(static_cast<const void*>(vertices.data()), vertices.size(), sizeof(T), type) {}
		~IndexBufferGl() override;
		void bind() override;
		void unbind() override;
		void setData(const void* data, size_t sz, size_t stride) override;

		[[nodiscard]] Id getID() const;
		
	private:
		Id mId = 0;
		UsageType mType = UsageType::STATIC_DRAW;

		IndexBufferGl(const void* data, size_t sz, size_t stride, UsageType type);
	};
}
#endif
