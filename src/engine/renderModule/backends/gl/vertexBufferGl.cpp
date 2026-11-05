#include "vertexBufferGl.h"
#ifdef OPENGL_BACKEND
#include <coreModule/graphicsWrapper.hpp>

namespace IKIGAI::INTERNAL
{
	constexpr GLenum UsageTypeMap[] = {
		GL_STREAM_DRAW,
		GL_STREAM_READ,
		GL_STREAM_COPY,
		GL_STATIC_DRAW,
		GL_STATIC_READ,
		GL_STATIC_COPY,
		GL_DYNAMIC_DRAW,
		GL_DYNAMIC_READ,
		GL_DYNAMIC_COPY,
	};

	constexpr GLenum AttributeTypeMap[]{
		GL_BYTE,
		GL_UNSIGNED_BYTE,
		GL_SHORT,
		GL_UNSIGNED_SHORT,
		GL_INT,
		GL_UNSIGNED_INT,
		GL_FLOAT,
#ifndef USING_GLES
		GL_DOUBLE,
#endif
	};
}


IKIGAI::RENDER::VertexBufferGl::VertexBufferGl(const void* data, size_t sz, size_t stride, UsageType type) : VertexBufferInterface(sz, stride), mType(type) {
	glGenBuffers(1, &mId);
	if (data) {
		VertexBufferGl::setData(data, sz, stride);
	}
}

IKIGAI::RENDER::VertexBufferGl::~VertexBufferGl() {
	glDeleteBuffers(1, &mId);
}

void IKIGAI::RENDER::VertexBufferGl::bind() {
	glBindBuffer(GL_ARRAY_BUFFER, mId);
}

void IKIGAI::RENDER::VertexBufferGl::unbind() {
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void IKIGAI::RENDER::VertexBufferGl::setData(const void* data, size_t sz, size_t stride) {
	requireOwnerThread();
	bind();
	glBufferData(GL_ARRAY_BUFFER, sz * stride, data, INTERNAL::UsageTypeMap[static_cast<int>(mType)]);
#if IKIGAI_ENABLE_MEMORY_TRACKING
    TrackBufferMemoryGl(mMemory, GL_ARRAY_BUFFER, mId, PROFILER::GpuResourceKind::VertexBuffer);
#endif
}

IKIGAI::RENDER::VertexBufferGl::Id IKIGAI::RENDER::VertexBufferGl::getID() const {
	return mId;
}

void IKIGAI::RENDER::VertexBufferGl::bindAttribute(unsigned index, int size, unsigned type, bool normalized, int stride, const void* pointer) {
	bind();
	glEnableVertexAttribArray(index);
	glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

void IKIGAI::RENDER::VertexBufferGl::bindIAttribute(unsigned index, int size, unsigned type, int stride, const void* pointer) {
	bind();
	glVertexAttribIPointer(index, size, type, stride, pointer);
	glEnableVertexAttribArray(index);
}

void IKIGAI::RENDER::VertexBufferGl::setSubData(long long offset, long long size, const void* data) {
	requireOwnerThread();
	bind();
	glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
}

// Vertex Array impl

IKIGAI::RENDER::VertexArray::VertexArray() {
	glGenVertexArrays(1, &mId);
	glBindVertexArray(mId);
}

IKIGAI::RENDER::VertexArray::~VertexArray() {
	glDeleteVertexArrays(1, &mId);
}

void IKIGAI::RENDER::VertexArray::bind() const {
	glBindVertexArray(mId);
}

void IKIGAI::RENDER::VertexArray::unbind() const {
	glBindVertexArray(0);
}

IKIGAI::RENDER::VertexArray::Id IKIGAI::RENDER::VertexArray::getID() const {
	return mId;
}

void IKIGAI::RENDER::VertexArray::bindAttribute(unsigned int attribute, VertexBufferGl& vertexBuffer, AttributeType type, unsigned int count, unsigned int stride, intptr_t offset) const {
	bind();
	vertexBuffer.bind();
	if (type == AttributeType::INT) {
		glVertexAttribIPointer(attribute, static_cast<GLint>(count), INTERNAL::AttributeTypeMap[static_cast<size_t>(type)], static_cast<GLsizei>(stride), reinterpret_cast<const GLvoid*>(offset));
	}
	else {
		glVertexAttribPointer(attribute, static_cast<GLint>(count), INTERNAL::AttributeTypeMap[static_cast<size_t>(type)], GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<const GLvoid*>(offset));
	}
	glEnableVertexAttribArray(attribute);
}

#endif


