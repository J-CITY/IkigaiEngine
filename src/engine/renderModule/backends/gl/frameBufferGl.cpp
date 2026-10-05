#include "frameBufferGl.h"
#ifdef OPENGL_BACKEND
#include "textureGl.h"

#include <coreModule/graphicsWrapper.hpp>
#include <iostream>

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

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
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, std::static_pointer_cast<TextureGl>(t)->id, 0);
		checkGlError("glFramebufferTexture2D");
		i++;
	}
	if (attachments.size()) {
//#ifndef USING_GLES
		glDrawBuffers(attachments.size(), attachments.data());
//#endif
	}
	if (!mDepth) {
		const unsigned int SCR_WIDTH = mTextures[0]->getWidth();
		const unsigned int SCR_HEIGHT = mTextures[0]->getHeight();
		std::cout << SCR_WIDTH << " " << SCR_HEIGHT << std::endl;
		
		unsigned int rboDepth;
		glGenRenderbuffers(1, &rboDepth);
		checkGlError("glGenRenderbuffers");
		glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
		checkGlError("glBindRenderbuffer");
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, SCR_WIDTH, SCR_HEIGHT);
		checkGlError("glRenderbufferStorage");
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);
		checkGlError("glFramebufferRenderbuffer");

		// finally check if framebuffer is complete
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << glCheckFramebufferStatus(GL_FRAMEBUFFER) << std::endl;
	}
	else {
		if (mDepth->getType() == TextureType::TEXTURE_2D) {
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
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
#endif
		int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
			throw 0;
		}
	}
	unbind();
}

void FrameBufferGl::bind() {
	glBindFramebuffer(GL_FRAMEBUFFER, mId);
}

void FrameBufferGl::unbind() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

FrameBufferGl::FrameBufferGl(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth): mTextures(textures), mDepth(depth) {
	create();
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



