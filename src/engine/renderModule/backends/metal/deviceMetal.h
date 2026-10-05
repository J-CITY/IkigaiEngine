#pragma once
#ifdef METAL_BACKEND

#import <Metal/Metal.h>

namespace IKIGAI::RENDER {
	// Active MTLDevice for the current Metal driver. Shader and resource
	// constructors use this so their signatures match the other backends.
	class DeviceMetal {
	public:
		static id<MTLDevice> Get();
	};
}

#endif
