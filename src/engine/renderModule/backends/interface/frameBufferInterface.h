#pragma once

#include <memory>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
	class TextureInterface;

	class FrameBufferInterface {
	protected:
		unsigned mMip = 0;
		unsigned mLayer = 0;
		size_t mWidth = 0;
		size_t mHeight = 0;
		unsigned mId = 0;
	public:
		virtual ~FrameBufferInterface() {}
		unsigned getMip() const { return mMip; }
		unsigned getLayer() const { return mLayer; }
		size_t getWidth() const {
			return mWidth;
		}
		size_t getHeight() const {
			return mHeight;
		}
		unsigned getId() const {
			return mId;
		}

		virtual const std::vector<std::shared_ptr<TextureInterface>>& getTextures() const = 0;
		virtual const std::shared_ptr<TextureInterface>& getDepth() const = 0;
	};

	[[nodiscard]] std::string AttachmentSignatureKey(const FrameBufferInterface& framebuffer, unsigned samples);
}
