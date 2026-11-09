#pragma once
#include <cstdint>

namespace IKIGAI::RENDER {
enum class GraphImageAccess { DepthAttachmentWrite, StorageWrite, StorageRead, ComputeSampledRead, GraphicsSampledRead };
enum class GraphImageAspect { Color, Depth };

struct GraphImageBarrier {
	GraphImageAccess Before = GraphImageAccess::StorageWrite;
	GraphImageAccess After = GraphImageAccess::StorageRead;
	GraphImageAspect Aspect = GraphImageAspect::Color;
	uint32_t FirstMip = 0;
	uint32_t MipCount = 0; // Zero covers the remaining mips.
	uint32_t FirstLayer = 0;
	uint32_t LayerCount = 0; // Zero covers the remaining layers.
};
}
