#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "interface/driverInterface.h"

namespace IKIGAI::WINDOW {
	class Window;
}

namespace IKIGAI::RENDER {
	const char* BackendToString(RenderSettings::Backend backend);
	std::optional<RenderSettings::Backend> ParseBackendName(std::string_view name);
	bool IsBackendCompiled(RenderSettings::Backend backend);
	void ValidateBackendAvailable(RenderSettings::Backend backend);
	void SetCliRenderBackendOverride(std::string_view name);
	bool ApplyCliRenderBackendOverride(RenderSettings& settings);

	std::unique_ptr<DriverInterface> CreateRenderDriver(RenderSettings::Backend backend, WINDOW::Window& window);
}
