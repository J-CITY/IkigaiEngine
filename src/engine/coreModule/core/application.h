#pragma once
#include <optional>
#include <string>

struct android_app;
namespace IKIGAI::CORE {
struct ApplicationOptions {
	std::string assetsDirectory;
	std::string scene = "scenes/scene.json";
	std::string loadingScene;
	std::string pipeline;
	std::string renderBackend;
	std::string renderExecutor {};
	std::string upscaler;
	std::string upscalerQuality;
	std::string frameGeneration;
	std::optional<int> frameGenerationMultiplier;
	std::string storageRoot;
	std::string profile = "default";
	std::string contentRelease;
	std::string screenshot;
	std::string screenshotTexture;
	bool profileLoading = false;
	bool renderCpuOverlap = false;
	// Offline virtual mesh cooking (IKIGAI_VG_COOKER builds): cook cookVmesh into cookOutput and exit.
	std::string cookVmesh;
	std::string cookOutput;
	std::string cookSettings;
	std::optional<int> width;
	std::optional<int> height;
	std::optional<bool> fullscreen;
	std::optional<std::string> title;
};
struct PlatformContext {
	android_app* android = nullptr;
};
int RunApplication(int argc, char* argv[], PlatformContext platform = {});
}
