#include "frameBufferInterface.h"
#include "textureInterface.h"

namespace IKIGAI::RENDER {
std::string AttachmentSignatureKey(const FrameBufferInterface& framebuffer, unsigned samples) {
	const auto& colors = framebuffer.getTextures();
	std::string key = std::to_string(samples) + ":" + std::to_string(colors.size());
	for (const auto& color : colors) {
		key += ":" + std::to_string(static_cast<int>(color->getFormat()));
	}
	const auto& depth = framebuffer.getDepth();
	key += depth ? ":d" + std::to_string(static_cast<int>(depth->getFormat())) : ":n";
	return key;
}
}
