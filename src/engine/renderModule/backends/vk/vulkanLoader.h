#pragma once

namespace IKIGAI::RENDER {
	// Absolute or bare name that dlopen/SDL can load, or nullptr if nothing was found.
	const char* FindVulkanLoaderPath();
}
