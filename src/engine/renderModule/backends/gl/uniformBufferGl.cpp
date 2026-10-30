#include "uniformBufferGl.h"
#ifdef OPENGL_BACKEND
#include <coreModule/graphicsWrapper.hpp>
#include <cstring>
#include <stdexcept>

IKIGAI::RENDER::UniformBufferGl::UniformBufferGl(const void* data, size_t sz)
	: UniformBufferInterface(sz) {
	glGenBuffers(1, &mId);

	bind();
	glBufferData(GL_UNIFORM_BUFFER, sz, nullptr, GL_STATIC_DRAW);
#if IKIGAI_ENABLE_MEMORY_TRACKING
	TrackBufferMemoryGl(mMemory, GL_UNIFORM_BUFFER, mId, PROFILER::GpuResourceKind::UniformBuffer);
#endif
	unbind();

	// #ifndef USING_GLES
	if (data) {
		UniformBufferGl::setData(data, sz);
	}
	// #endif
}

IKIGAI::RENDER::UniformBufferGl::UniformBufferGl(size_t sz)
	: UniformBufferGl(nullptr, sz) { }

IKIGAI::RENDER::UniformBufferGl::~UniformBufferGl() {
	glDeleteBuffers(1, &mId);
}

void IKIGAI::RENDER::UniformBufferGl::setData(const void* data, size_t sz, size_t offset) {
	if (offset > mSizeByte || sz > mSizeByte - offset || (!data && sz)) {
		throw std::invalid_argument("Invalid OpenGL uniform buffer update");
	}
	if (!sz) {
		return;
	}
	const bool complete = offset == 0 && sz == mSizeByte;
	if (complete && mLastUpload.size() == sz && std::memcmp(mLastUpload.data(), data, sz) == 0) {
		return;
	}
	bind();
	if (complete) {
		// Replace the store so in-flight draws can keep reading their previous data.
		glBufferData(GL_UNIFORM_BUFFER, sz, data, GL_STREAM_DRAW);
		const auto* bytes = static_cast<const std::byte*>(data);
		mLastUpload.assign(bytes, bytes + sz);
	}
	else {
		glBufferSubData(GL_UNIFORM_BUFFER, offset, sz, data);
		mLastUpload.clear();
	}
	unbind();
}

void IKIGAI::RENDER::UniformBufferGl::bind() {
	glBindBuffer(GL_UNIFORM_BUFFER, mId);
}

void IKIGAI::RENDER::UniformBufferGl::bindToShader(unsigned slot) {
	glBindBufferRange(GL_UNIFORM_BUFFER, slot, mId, 0, static_cast<long long>(mSizeByte));
}

void IKIGAI::RENDER::UniformBufferGl::unbind() {
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

unsigned IKIGAI::RENDER::UniformBufferGl::getId() const {
	return mId;
}
#endif
