#include "vulkanLoader.h"

#if defined(__APPLE__)
#include <dlfcn.h>
#include <cstdlib>
#endif

const char* IKIGAI::RENDER::FindVulkanLoaderPath() {
#if !defined(__APPLE__)
	return nullptr;
#else
	static const char* cached = nullptr;
	static bool tried = false;
	if (tried) {
		return cached;
	}
	tried = true;

	// MoltenVK's default Metal argument buffers skip the auxiliary buffer that
	// holds runtime SSBO lengths (spvBufferSizeConstants, index 29). The unsized
	// lights[] array needs that binding. Turn argument buffers off before the
	// loader pulls MoltenVK in.
	setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0", 0);

	// volk and SDL only try bare names and /usr/local. Apple Silicon Homebrew
	// installs the loader under /opt/homebrew, which a GUI app does not search.
	static const char* kPaths[] = {
		"/opt/homebrew/lib/libvulkan.1.dylib",
		"/opt/homebrew/lib/libvulkan.dylib",
		"/usr/local/lib/libvulkan.1.dylib",
		"/usr/local/lib/libvulkan.dylib",
		"/opt/homebrew/lib/libMoltenVK.dylib",
		"/usr/local/lib/libMoltenVK.dylib",
		"libvulkan.1.dylib",
		"libvulkan.dylib",
		"libMoltenVK.dylib",
	};
	for (const char* path : kPaths) {
		void* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
		if (!module) {
			continue;
		}
		if (dlsym(module, "vkGetInstanceProcAddr")) {
			cached = path;
			return cached;
		}
		dlclose(module);
	}
	return nullptr;
#endif
}
