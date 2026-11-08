#include "integerColorAttachment.h"

#include <stdexcept>

#include "resourceStruct.h"

namespace IKIGAI::RENDER {
void ValidateRg32IntAttachment(const TextureResource& descriptor, bool hasInitialData) {
	if (descriptor.texType != TextureType::TEXTURE_2D || descriptor.width <= 0 || descriptor.height <= 0 || descriptor.depth < 0
		|| descriptor.depth > 1 || descriptor.useMipmap || descriptor.mipMapCount < 0 || descriptor.mipMapCount > 1
		|| (descriptor.channels != 0 && descriptor.channels != 2) || descriptor.minFilter != MinMagFilter::NEAREST
		|| descriptor.magFilter != MinMagFilter::NEAREST || descriptor.isFloat || hasInitialData || !descriptor.pathTexture.empty()
		|| !descriptor.colorData.empty()) {
		throw std::invalid_argument("RG32_INT needs a point-filtered 2D attachment with one mip and no initial data");
	}
}

float Dx12IntegerRtvClearComponent(int32_t value) {
	const auto native = static_cast<float>(value);
	if (static_cast<double>(native) != static_cast<double>(value)) {
		throw std::invalid_argument("DX12 integer RTV clear value is not exactly representable as float");
	}
	return native;
}
}
