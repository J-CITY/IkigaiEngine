#include "storageBufferGl.h"
#ifdef OPENGL_BACKEND
#include <coreModule/graphicsWrapper.hpp>

namespace {
GLenum StorageBufferTarget() {
#if defined(IKIGAI_GLES_HAS_SSBO) || !defined(USING_GLES)
	return GL_SHADER_STORAGE_BUFFER;
#elif defined(GL_UNIFORM_BUFFER)
	return GL_UNIFORM_BUFFER;
#else
	return 0;
#endif
}
}

IKIGAI::RENDER::StorageBufferGl::StorageBufferGl(const void* data, size_t sz, size_t stride) : StorageBufferInterface(sz, stride) {
	const auto target = StorageBufferTarget();
	if (!target) {
		return;
	}
	glGenBuffers(1, &mId);
	if (mSizeByte > 0 || data) {
		StorageBufferGl::setData(data, sz, stride);
	}
}

IKIGAI::RENDER::StorageBufferGl::~StorageBufferGl() {
	if (mId) {
		glDeleteBuffers(1, &mId);
	}
}

void IKIGAI::RENDER::StorageBufferGl::setData(const void* data, size_t sz, size_t stride) {
	const auto target = StorageBufferTarget();
	if (!target || !mId) {
		return;
	}
	glBindBuffer(target, mId);
	glBufferData(target, static_cast<GLsizeiptr>(sz * stride), data, GL_DYNAMIC_DRAW);
	glBindBuffer(target, 0);
}

void IKIGAI::RENDER::StorageBufferGl::setSubData(const void* data, size_t sz, size_t offset) {
	const auto target = StorageBufferTarget();
	if (!target || !mId) {
		return;
	}
	glBindBuffer(target, mId);
	glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(sz), data);
	glBindBuffer(target, 0);
}

IKIGAI::RENDER::StorageBufferGl::Id IKIGAI::RENDER::StorageBufferGl::getId() const {
	return mId;
}

//void ShaderStorageBufferGl::bind(unsigned val) {
//#ifndef USING_GLES
//	mBindId = val;
//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, val, mId);
//#endif
//}
//
//void ShaderStorageBufferGl::unbind() {
//#ifndef USING_GLES
//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, mBindId, 0);
//#endif
//}

#endif
