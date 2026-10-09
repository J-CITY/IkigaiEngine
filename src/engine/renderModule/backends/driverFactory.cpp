#include "driverFactory.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#ifdef OPENGL_BACKEND
#include "gl/driverGl.h"
#endif
#ifdef VULKAN_BACKEND
#include "vk/driverVk.h"
#endif
#ifdef DX12_BACKEND
#include "dx12/driverDx12.h"
#endif
#ifdef METAL_BACKEND
namespace IKIGAI::RENDER {
	std::unique_ptr<DriverInterface> CreateDriverMetal();
}
#endif

namespace IKIGAI::RENDER {
	namespace {
		std::optional<std::string> gCliBackend;
		std::string ToLower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}
	}

	const char* BackendToString(RenderSettings::Backend backend) {
		switch (backend) {
		case RenderSettings::Backend::OPENGL: return "opengl";
		case RenderSettings::Backend::VULKAN: return "vulkan";
		case RenderSettings::Backend::DIRECTX12: return "directx12";
		case RenderSettings::Backend::METAL: return "metal";
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
		switch (backend) {
#ifdef OPENGL_BACKEND
		case RenderSettings::Backend::OPENGL: return true;
#endif
#ifdef VULKAN_BACKEND
		case RenderSettings::Backend::VULKAN: return true;
#endif
#ifdef DX12_BACKEND
		case RenderSettings::Backend::DIRECTX12: return true;
#endif
#ifdef METAL_BACKEND
		case RenderSettings::Backend::METAL: return true;
#endif
		default: return false;
		}
	}

	void ValidateBackendAvailable(RenderSettings::Backend backend) {
		if (IsBackendCompiled(backend)) {
			return;
		}
		throw std::runtime_error(std::string("Render backend '") + BackendToString(backend)
			+ "' is requested but was not compiled into this binary");
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

	void ResolveRenderBackend(RenderSettings& settings) {
		if (!ApplyCliRenderBackendOverride(settings)) {
			throw std::runtime_error("Invalid --render-backend value");
		}
		if (!gCliBackend && !IsBackendCompiled(settings.backend)) {
			std::optional<RenderSettings::Backend> soleBackend;
			for (const auto candidate : {RenderSettings::Backend::OPENGL,
				RenderSettings::Backend::VULKAN, RenderSettings::Backend::DIRECTX12,
				RenderSettings::Backend::METAL}) {
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
		std::unique_ptr<DriverInterface> driver;
		switch (backend) {
#ifdef OPENGL_BACKEND
		case RenderSettings::Backend::OPENGL:
			driver = std::make_unique<DriverGl>();
			break;
#endif
#ifdef VULKAN_BACKEND
		case RenderSettings::Backend::VULKAN:
			driver = std::make_unique<DriverVk>();
			break;
#endif
#ifdef DX12_BACKEND
		case RenderSettings::Backend::DIRECTX12:
			driver = std::make_unique<DriverDx12>();
			break;
#endif
#ifdef METAL_BACKEND
		case RenderSettings::Backend::METAL:
			driver = CreateDriverMetal();
			break;
#endif
		default:
			break;
		}
		if (!driver) {
			throw std::runtime_error("Failed to create render driver");
		}
		DriverInterface::SetActive(driver.get());
		return driver;
	}
}
