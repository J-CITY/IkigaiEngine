#include "driverFactory.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif

namespace IKIGAI::RENDER {
namespace {
	std::unordered_map<RenderSettings::Backend, CreateDriverFn>& backendFactories() {
		static std::unordered_map<RenderSettings::Backend, CreateDriverFn> factories;
		return factories;
	}
}
void RegisterRenderBackend(RenderSettings::Backend backend, CreateDriverFn create) {
	backendFactories().insert_or_assign(backend, create);
}
namespace {
	std::optional<std::string> gCliBackend;
	std::optional<std::string> gCliPipeline;
	std::optional<UpscalerType> gCliUpscaler;
	std::optional<UpscalerQuality> gCliUpscalerQuality;
	std::optional<FrameGenerationType> gCliFrameGeneration;
	std::optional<unsigned> gCliFrameGenerationMultiplier;
	std::optional<RenderSettings::PresentMode> gCliPresentMode;
	std::string ToLower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}
}

const char* BackendToString(RenderSettings::Backend backend) {
	switch (backend) {
	case RenderSettings::Backend::OPENGL:
		return "opengl";
	case RenderSettings::Backend::VULKAN:
		return "vulkan";
	case RenderSettings::Backend::DIRECTX12:
		return "directx12";
	case RenderSettings::Backend::METAL:
		return "metal";
	}
	return "unknown";
}

std::optional<RenderSettings::Backend> ParseBackendName(std::string_view name) {
	const auto lower = ToLower(std::string(name));
	if (lower == "opengl" || lower == "gl") {
		return RenderSettings::Backend::OPENGL;
	}
	if (lower == "vulkan" || lower == "vk") {
		return RenderSettings::Backend::VULKAN;
	}
	if (lower == "directx12" || lower == "dx12" || lower == "d3d12") {
		return RenderSettings::Backend::DIRECTX12;
	}
	if (lower == "metal") {
		return RenderSettings::Backend::METAL;
	}
	return std::nullopt;
}

bool IsBackendCompiled(RenderSettings::Backend backend) {
	return backendFactories().contains(backend);
}

void ValidateBackendAvailable(RenderSettings::Backend backend) {
	if (IsBackendCompiled(backend)) {
		return;
	}
	throw std::runtime_error(std::string("Render backend '") + BackendToString(backend) + "' is requested but was not compiled into this binary");
}

void SetCliRenderBackendOverride(std::string_view name) {
	gCliBackend = std::string(name);
}

bool ApplyCliRenderBackendOverride(RenderSettings& settings) {
	if (!gCliBackend) {
		return true;
	}
	const auto parsed = ParseBackendName(*gCliBackend);
	if (!parsed) {
		return false;
	}
	settings.backend = *parsed;
	return true;
}

void SetCliRenderPipelineOverride(std::string_view path) {
	gCliPipeline = std::string(path);
}
bool HasCliRenderPipelineOverride() {
	return gCliPipeline.has_value();
}
void SetCliUpscalerOverride(std::string_view name) {
	if (name == "off") {
		gCliUpscaler = UpscalerType::OFF;
	}
	else if (name == "fsr") {
		gCliUpscaler = UpscalerType::FSR;
	}
	else if (name == "dlss") {
		gCliUpscaler = UpscalerType::DLSS;
	}
	else if (name == "auto") {
		gCliUpscaler = UpscalerType::AUTO;
	}
	else {
		throw std::runtime_error("Invalid --upscaler; expected off/fsr/dlss/auto");
	}
}
void SetCliUpscalerQualityOverride(std::string_view name) {
	if (name == "quality") {
		gCliUpscalerQuality = UpscalerQuality::QUALITY;
	}
	else if (name == "balanced") {
		gCliUpscalerQuality = UpscalerQuality::BALANCED;
	}
	else if (name == "performance") {
		gCliUpscalerQuality = UpscalerQuality::PERFORMANCE;
	}
	else {
		throw std::runtime_error("Invalid --upscaler-quality; expected quality/balanced/performance");
	}
}

void SetCliFrameGenerationOverride(std::string_view name) {
	if (name == "off") {
		gCliFrameGeneration = FrameGenerationType::OFF;
	}
	else if (name == "fsr") {
		gCliFrameGeneration = FrameGenerationType::FSR;
	}
	else if (name == "dlss") {
		gCliFrameGeneration = FrameGenerationType::DLSS;
	}
	else if (name == "auto") {
		gCliFrameGeneration = FrameGenerationType::AUTO;
	}
	else {
		throw std::runtime_error("Invalid --frame-generation; expected off/fsr/dlss/auto");
	}
}
void SetCliFrameGenerationMultiplier(unsigned multiplier) {
	if (multiplier < 2 || multiplier > 6) {
		throw std::runtime_error("Frame generation multiplier must be in [2,6]");
	}
	gCliFrameGenerationMultiplier = multiplier;
}
void SetCliPresentModeOverride(std::string_view name) {
	if (name == "default") {
		gCliPresentMode = RenderSettings::PresentMode::DEFAULT;
	}
	else if (name == "fifo" || name == "vsync") {
		gCliPresentMode = RenderSettings::PresentMode::FIFO;
	}
	else if (name == "mailbox") {
		gCliPresentMode = RenderSettings::PresentMode::MAILBOX;
	}
	else if (name == "immediate" || name == "novsync") {
		gCliPresentMode = RenderSettings::PresentMode::IMMEDIATE;
	}
	else {
		throw std::runtime_error("Invalid --present-mode; expected default/fifo/mailbox/immediate");
	}
}
const char* PresentModeToString(RenderSettings::PresentMode mode) {
	switch (mode) {
	case RenderSettings::PresentMode::FIFO:
		return "fifo";
	case RenderSettings::PresentMode::MAILBOX:
		return "mailbox";
	case RenderSettings::PresentMode::IMMEDIATE:
		return "immediate";
	default:
		return "default";
	}
}
void ResolveRenderBackend(RenderSettings& settings) {
	if (gCliPresentMode) {
		settings.presentMode = *gCliPresentMode;
	}
	if (gCliFrameGeneration) {
		settings.frameGeneration.technology = *gCliFrameGeneration;
	}
	if (gCliFrameGenerationMultiplier) {
		settings.frameGeneration.multiplier = *gCliFrameGenerationMultiplier;
	}
	if (gCliUpscaler) {
		settings.upscaler.technology = *gCliUpscaler;
	}
	if (gCliUpscalerQuality) {
		settings.upscaler.quality = *gCliUpscalerQuality;
	}
	if (gCliPipeline) {
		settings.pipeline = *gCliPipeline;
	}
	if (!ApplyCliRenderBackendOverride(settings)) {
		throw std::runtime_error("Invalid --render-backend value");
	}
	if (!gCliBackend && !IsBackendCompiled(settings.backend)) {
		std::optional<RenderSettings::Backend> soleBackend;
		for (const auto candidate : {
			 RenderSettings::Backend::OPENGL, RenderSettings::Backend::VULKAN, RenderSettings::Backend::DIRECTX12, RenderSettings::Backend::METAL }) {
			if (!IsBackendCompiled(candidate)) {
				continue;
			}
			if (soleBackend) {
				// Several APIs are available: do not guess which one to use.
				ValidateBackendAvailable(settings.backend);
			}
			soleBackend = candidate;
		}
		if (soleBackend) {
			settings.backend = *soleBackend;
		}
	}
	ValidateBackendAvailable(settings.backend);
}

std::unique_ptr<DriverInterface> CreateRenderDriver(RenderSettings::Backend backend, WINDOW::Window&) {
	ValidateBackendAvailable(backend);
	const auto found = backendFactories().find(backend);
	if (found == backendFactories().end()) {
		throw std::runtime_error("Failed to create render driver");
	}
	std::unique_ptr<DriverInterface> driver = found->second();
	if (!driver) {
		throw std::runtime_error("Failed to create render driver");
	}
	DriverInterface::SetActive(driver.get());
	return driver;
}
}
