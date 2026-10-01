#pragma once
#include <memory>
#include "renderModule/backends/interface/driverInterface.h"

struct SDL_Window;

namespace IKIGAI::WINDOW {
	class Window;
}

namespace IKIGAI::RENDER {
	class DriverInterface;
	class TextureInterface;
}

namespace IKIGAI::IMGUI {
	class IImGuiBackend {
	public:
		virtual ~IImGuiBackend() = default;
		virtual bool init(WINDOW::Window& window, RENDER::DriverInterface& driver) = 0;
		virtual void shutdown() = 0;
		virtual void newFrame() = 0;
		virtual void endFrame() = 0;
		virtual void renderDrawData() = 0;
		virtual void* registerGpuTexture(RENDER::TextureInterface& texture) = 0;
		virtual void invalidateDeviceObjects() = 0;
		virtual void processEvent(const void* sdlEvent) = 0;
	};

	std::unique_ptr<IImGuiBackend> CreateImGuiBackend(RENDER::RenderSettings::Backend backend);
	IImGuiBackend* Get();
	void SetActive(IImGuiBackend* backend);
	void ShutdownActive();
}
