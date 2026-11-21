#include "frameBufferGl.h"
#ifdef OPENGL_BACKEND
#include "textureGl.h"

#include <iostream>
#include <renderModule/backends/graphicsWrapper.hpp>
#include <stdexcept>
#if defined(USE_SDL) && defined(__APPLE__) && TARGET_OS_IPHONE
#include <SDL3/SDL.h>
#endif

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

void IKIGAI::RENDER::BindDefaultFramebufferGl() {
	GLuint framebuffer = 0;
#if defined(USE_SDL) && defined(__APPLE__) && TARGET_OS_IPHONE
	// UIKit renders into an FBO owned by SDL; framebuffer 0 is not the screen.
	if (auto* window = SDL_GL_GetCurrentWindow()) {
		framebuffer = static_cast<GLuint>(SDL_GetNumberProperty(
			SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0));
	}
#endif
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

void checkGlError(const char* op, ...) {
	va_list params;
	char buf[BUFSIZ];

	va_start(params, op);
	vsprintf(buf, op, params);
	int count = 0;
	for (GLint error = glGetError(); error; error = glGetError()) {
		switch (error) {
		case GL_INVALID_ENUM:
			std::cout << "after " << buf << "() glError: GL_INVALID_ENUM\n";
			break;
		case GL_INVALID_VALUE:
			std::cout << "after " << buf << "() glError: GL_INVALID_VALUE\n";
			break;
		case GL_INVALID_OPERATION:
			std::cout << "after " << buf << "() glError: GL_INVALID_OPERATION\n";
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			std::cout << "after " << buf << "() glError: GL_INVALID_FRAMEBUFFER_OPERATION\n";
			break;
		case GL_OUT_OF_MEMORY:
			std::cout << "after " << buf << "() glError: GL_OUT_OF_MEMORY\n";
			break;
		default:
			break;
		}
		if (++count > 10) {
			std::cout << "Too many GL errors, breaking loop\n";
			break;
		}
	}
	va_end(params);
}
void FrameBufferGl::create() {
	if (mTextures.empty() && !mDepth) throw std::runtime_error("Framebuffer needs a color or depth attachment");
	auto sizeTexture = mTextures.empty() ? mDepth : mTextures.front();
	mWidth = std::max<size_t>(1,sizeTexture->getWidth() >> mMip);
	mHeight = std::max<size_t>(1,sizeTexture->getHeight() >> mMip);
	GLint previousRenderbuffer = 0;
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
	glGenFramebuffers(1, &mId);
	std::cout << mId << std::endl;
	checkGlError("glGenFramebuffers");
	bind();
	checkGlError("glBindFramebuffer");

	int i = 0;
	std::vector<unsigned> attachments;
	attachments.resize(mTextures.size());
	for (auto t : mTextures) {
		attachments[i] = GL_COLOR_ATTACHMENT0 + i;
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
            t->getType()==TextureType::TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP_POSITIVE_X+mLayer : GL_TEXTURE_2D, std::static_pointer_cast<TextureGl>(t)->id, mMip);
		checkGlError("glFramebufferTexture2D");
		i++;
	}
	if (attachments.size()) {
//#ifndef USING_GLES
		glDrawBuffers(attachments.size(), attachments.data());
//#endif
	}
	if (!mDepth) {
		const unsigned int SCR_WIDTH = mWidth;
		const unsigned int SCR_HEIGHT = mHeight;
		std::cout << SCR_WIDTH << " " << SCR_HEIGHT << std::endl;
		
		glGenRenderbuffers(1, &mDepthRenderbuffer);
		checkGlError("glGenRenderbuffers");
		glBindRenderbuffer(GL_RENDERBUFFER, mDepthRenderbuffer);
		checkGlError("glBindRenderbuffer");
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, SCR_WIDTH, SCR_HEIGHT);
#if IKIGAI_ENABLE_MEMORY_TRACKING
        TrackRenderbufferMemoryGl(mDepthMemory, mDepthRenderbuffer);
#endif
		checkGlError("glRenderbufferStorage");
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthRenderbuffer);
		checkGlError("glFramebufferRenderbuffer");

		// finally check if framebuffer is complete
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << glCheckFramebufferStatus(GL_FRAMEBUFFER) << std::endl;
	}
	else {
		if (mDepth->getType() == TextureType::TEXTURE_2D || mDepth->getType() == TextureType::DEPTH) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, std::static_pointer_cast<TextureGl>(mDepth)->id, 0);
		}
		else
		{
#ifndef USING_GLES
			// support only in es 3.2
			glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, std::static_pointer_cast<TextureGl>(mDepth)->id, 0);
#endif
		}
#ifndef USING_GLES
		//I dont know why glDrawBuffer undeclared 
		if (mTextures.empty()) {
			glDrawBuffer(GL_NONE);
			glReadBuffer(GL_NONE);
		}
#endif
		int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
			throw 0;
		}
	}
	unbind();
	// SDL's UIKit swap presents its color renderbuffer, which must remain bound.
	glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
}

void FrameBufferGl::bind() {
	glBindFramebuffer(GL_FRAMEBUFFER, mId);
}

void FrameBufferGl::unbind() {
	BindDefaultFramebufferGl();
}

FrameBufferGl::FrameBufferGl(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth, unsigned mip, unsigned layer): mTextures(textures), mDepth(depth) {
	mMip=mip; mLayer=layer;
	create();
}

FrameBufferGl::~FrameBufferGl() {
	if (mDepthRenderbuffer) glDeleteRenderbuffers(1, &mDepthRenderbuffer);
	if (mId) glDeleteFramebuffers(1, &mId);
}

const std::vector<std::shared_ptr<TextureInterface>>& FrameBufferGl::getTextures() const {
	return mTextures;
}

const std::shared_ptr<TextureInterface>& FrameBufferGl::getDepth() const {
	return mDepth;
}

void FrameBufferGl::CopyDepth(const FrameBufferGl& from, const FrameBufferGl& to, unsigned w, unsigned h) {
//#ifndef USING_GLES
	glBindFramebuffer(GL_READ_FRAMEBUFFER, from.mId);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to.mId);
	glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
//#endif
}
#endif



