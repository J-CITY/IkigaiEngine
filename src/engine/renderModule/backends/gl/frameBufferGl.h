#pragma once
#include "renderModule/backends/interface/frameBufferInterface.h"

#ifdef OPENGL_BACKEND
#include <memory>
#include <vector>

namespace IKIGAI::RENDER {
	void BindDefaultFramebufferGl();
	class TextureGl;

	class FrameBufferGl: public FrameBufferInterface {
	private:
		std::vector<std::shared_ptr<TextureInterface>> mTextures;
		std::shared_ptr<TextureInterface> mDepth;
	public:
		FrameBufferGl(const std::vector<std::shared_ptr<TextureInterface>>& textures, std::shared_ptr<TextureInterface> depth = nullptr);
		const std::vector<std::shared_ptr<TextureInterface>>& getTextures() const override;
		const std::shared_ptr<TextureInterface>& getDepth() const override;

		void bind();
		void unbind();

		static void CopyDepth(const FrameBufferGl& from, const FrameBufferGl& to, unsigned w, unsigned h);
	private:
		void create();
	};
}
#endif
