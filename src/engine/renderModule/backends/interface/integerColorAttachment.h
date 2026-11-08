#pragma once

#include <cstdint>

namespace IKIGAI::RENDER {
struct TextureResource;

void ValidateRg32IntAttachment(const TextureResource& descriptor, bool hasInitialData);
float Dx12IntegerRtvClearComponent(int32_t value);
}
