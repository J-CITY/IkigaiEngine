#pragma once
#include "interface/driverInterface.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace IKIGAI::WINDOW {
class Window;
}

namespace IKIGAI::RENDER {
const char* BackendToString(RenderSettings::Backend backend);
std::optional<RenderSettings::Backend> ParseBackendName(std::string_view name);
bool IsBackendCompiled(RenderSettings::Backend backend);
void ValidateBackendAvailable(RenderSettings::Backend backend);
void SetCliRenderBackendOverride(std::string_view name);
void SetCliRenderPipelineOverride(std::string_view path);
bool HasCliRenderPipelineOverride();
void SetCliUpscalerOverride(std::string_view name);
void SetCliUpscalerQualityOverride(std::string_view name);
void SetCliFrameGenerationOverride(std::string_view name);
void SetCliFrameGenerationMultiplier(unsigned multiplier);
// Accepts default/fifo/mailbox/immediate (also vsync=fifo, novsync=immediate).
void SetCliPresentModeOverride(std::string_view name);
void SetCliRenderExecutorOverride(std::string_view name);
const char* PresentModeToString(RenderSettings::PresentMode mode);
bool ApplyCliRenderBackendOverride(RenderSettings& settings);
// A shared asset config may target another platform. Only fall back when
// the binary contains exactly one backend and no explicit CLI selection.
void ResolveRenderBackend(RenderSettings& settings);

using CreateDriverFn = std::function<std::unique_ptr<DriverInterface>()>;
void RegisterRenderBackend(RenderSettings::Backend backend, CreateDriverFn create);
std::unique_ptr<DriverInterface> CreateRenderDriver(RenderSettings::Backend backend, WINDOW::Window& window);
}
