#include "indexBufferGl.h"
#ifdef OPENGL_BACKEND
#include <coreModule/graphicsWrapper.hpp>

namespace IKIGAI::INTERNAL {
	const GLenum UsageTypeMap[] = {
		GL_STREAM_DRAW,
//#ifndef USING_GLES
		GL_STREAM_READ,
		GL_STREAM_COPY,
//#endif
		GL_STATIC_DRAW,
//#ifndef USING_GLES
		GL_STATIC_READ,
		GL_STATIC_COPY,
//#endif
		GL_DYNAMIC_DRAW,
//#ifndef USING_GLES
		GL_DYNAMIC_READ,
		GL_DYNAMIC_COPY
//#endif
	};
}

IKIGAI::RENDER::IndexBufferGl::IndexBufferGl(const void* data, size_t sz, size_t stride, UsageType type) : IndexBufferInterface(sz, stride), mType(type) {
	glGenBuffers(1, &mId);
	if (data) {
		IndexBufferGl::setData(data, sz, stride);
	}
}

IKIGAI::RENDER::IndexBufferGl::~IndexBufferGl() {
	glDeleteBuffers(1, &mId);
}

void IKIGAI::RENDER::IndexBufferGl::bind() {
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mId);
}

void IKIGAI::RENDER::IndexBufferGl::unbind() {
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void IKIGAI::RENDER::IndexBufferGl::setData(const void* data, size_t sz, size_t stride) {
	requireOwnerThread();
	bind();
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sz * stride, data, INTERNAL::UsageTypeMap[static_cast<unsigned>(mType)]);
#if IKIGAI_ENABLE_MEMORY_TRACKING
    TrackBufferMemoryGl(mMemory, GL_ELEMENT_ARRAY_BUFFER, mId, PROFILER::GpuResourceKind::IndexBuffer);
#endif
}

IKIGAI::RENDER::IndexBufferGl::Id IKIGAI::RENDER::IndexBufferGl::getID() const {
	return mId;
}
#endif
