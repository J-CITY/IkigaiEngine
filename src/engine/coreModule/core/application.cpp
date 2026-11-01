#include "application.h"
#include "app.h"
#include "captureModule/screenshot.h"
#include "coreModule/config.h"
#include "renderModule/backends/driverFactory.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/contentRoot.h"
#if defined(IKIGAI_VG_COOKER)
#include "virtualGeometryModule/cook/vmeshCookModel.h"
#endif
#include <argParse.h>
#include <filesystem>
#include <iostream>

namespace IKIGAI::CORE {
namespace {
	ap::ArgParse makeParser(ApplicationOptions& options) {
		ap::ArgParse parser("IkigaiEngine", "Run a scene with optional application overrides.");
		auto stringOption = [&](const char* name, std::string& target, const char* description) {
			parser.addArg<std::string>({ name }, [&target](const std::string& value) { target = value; }, {}, description);
		};
		stringOption("--assets", options.assetsDirectory, "Assets directory containing engine/ and game/");
		stringOption("--scene", options.scene, "Initial scene asset");
		stringOption("--loading-scene", options.loadingScene, "Loading scene asset");
		stringOption("--pipeline", options.pipeline, "Render pipeline asset");
		stringOption("--render-backend", options.renderBackend, "opengl, vulkan, directx12 or metal");
		stringOption("--upscaler", options.upscaler, "Upscaler");
		stringOption("--upscaler-quality", options.upscalerQuality, "Upscaler quality");
		stringOption("--frame-generation", options.frameGeneration, "Frame generation mode");
		stringOption("--storage-root", options.storageRoot, "Writable application storage directory");
		stringOption("--profile", options.profile, "Player profile");
		stringOption("--content-release", options.contentRelease, "Local content release manifest");
		stringOption("--screenshot", options.screenshot, "Capture a screenshot");
		stringOption("--screenshot-texture", options.screenshotTexture, "Screenshot source texture");
#if defined(IKIGAI_VG_COOKER)
		stringOption("--cook-vmesh", options.cookVmesh, "Cook a model into a virtual mesh and exit");
		stringOption("--cook-output", options.cookOutput, "Output .vmesh path for --cook-vmesh");
		stringOption("--cook-settings", options.cookSettings, "Optional JSON cook settings for --cook-vmesh");
#endif
		parser.addArg<int>({ "--width" }, [&](int value) { options.width = value; }, {}, "Window width");
		parser.addArg<int>({ "--height" }, [&](int value) { options.height = value; }, {}, "Window height");
		parser.addArg<int>(
			{ "--frame-generation-multiplier" }, [&](int value) { options.frameGenerationMultiplier = value; }, {}, "Frame generation multiplier");
		parser.addArg<std::string>({ "--title" }, [&](const std::string& value) { options.title = value; }, {}, "Window title");
		parser.addArg<bool>({ "--fullscreen" }, [&](bool value) { options.fullscreen = value; }, {}, "Use fullscreen mode");
		parser.addArg<bool>({ "--windowed" }, [&](bool) { options.fullscreen = false; }, {}, "Use windowed mode");
		parser.addArg<bool>({ "--profile-loading" }, [&](bool value) { options.profileLoading = value; }, {}, "Report loading timings");
		return parser;
	}
	void applyOptions(const ApplicationOptions& options) {
		if ((options.width && *options.width <= 0) || (options.height && *options.height <= 0)) {
			throw std::invalid_argument("Window dimensions must be positive");
		}
		if (!options.assetsDirectory.empty()) {
			const auto directory = std::filesystem::absolute(std::filesystem::u8path(options.assetsDirectory)).lexically_normal();
			if (!std::filesystem::is_directory(directory / "engine") || !std::filesystem::is_directory(directory / "game")) {
				throw std::invalid_argument("Assets directory must contain engine/ and game/");
			}
			Config::ROOT = UTILS::NormalizeRootPath(directory.parent_path().generic_string());
			Config::ASSETS_PATH = UTILS::NormalizeRootPath(directory.filename().generic_string());
			Config::ENGINE_ASSETS_PATH = Config::ASSETS_PATH + "engine/";
			Config::USER_ASSETS_PATH = Config::ASSETS_PATH + "game/";
		}
		Config::WINDOW_WIDTH = options.width;
		Config::WINDOW_HEIGHT = options.height;
		Config::WINDOW_FULLSCREEN = options.fullscreen;
		Config::WINDOW_TITLE = options.title;
		Config::STORAGE_ROOT = options.storageRoot;
		Config::PROFILE_ID = options.profile;
		Config::CONTENT_RELEASE = options.contentRelease;
		Config::PROFILE_LOADING = options.profileLoading;
		if (!options.pipeline.empty()) {
			RENDER::SetCliRenderPipelineOverride(options.pipeline);
		}
		if (!options.renderBackend.empty()) {
			RENDER::SetCliRenderBackendOverride(options.renderBackend);
		}
		if (!options.upscaler.empty()) {
			RENDER::SetCliUpscalerOverride(options.upscaler);
		}
		if (!options.upscalerQuality.empty()) {
			RENDER::SetCliUpscalerQualityOverride(options.upscalerQuality);
		}
		if (!options.frameGeneration.empty()) {
			RENDER::SetCliFrameGenerationOverride(options.frameGeneration);
		}
		if (options.frameGenerationMultiplier) {
			if (*options.frameGenerationMultiplier < 1) {
				throw std::invalid_argument("Frame generation multiplier must be positive");
			}
			RENDER::SetCliFrameGenerationMultiplier(static_cast<unsigned>(*options.frameGenerationMultiplier));
		}
	}
}
int RunApplication(int argc, char* argv[], PlatformContext platform) {
	try {
		ApplicationOptions options;
		auto parser = makeParser(options);
		const auto parsed = parser.parse(argc, argv);
		if (parsed.status == ap::ArgParse::Status::Help) {
			return 0;
		}
		if (parsed.status == ap::ArgParse::Status::Error) {
			std::cerr << parsed.error << '\n';
			return 2;
		}
		applyOptions(options);
#if defined(IKIGAI_VG_COOKER)
		if (!options.cookVmesh.empty()) {
			return VG::RunVMeshCookCommand(VG::VMeshCookCommand { options.cookVmesh, options.cookOutput, options.cookSettings });
		}
#endif
#ifdef DX12_BACKEND
		App::hInstance = GetModuleHandleW(nullptr);
#endif
#ifdef OCULUS
		if (!platform.android) {
			throw std::invalid_argument("Android application context is required");
		}
		App application(platform.android);
#else
		App application;
#endif
		if (!options.loadingScene.empty()) {
			application.getCore()->sceneManager->prepareLoadingScene(options.loadingScene);
		}
		auto initialSceneLoad = application.getCore()->sceneManager->loadFromFileAsync(options.scene);
		if (!options.screenshot.empty()
			&& !CAPTURE::ScreenshotService::Get().request(std::filesystem::u8path(options.screenshot), options.screenshotTexture)) {
			throw std::runtime_error("Cannot request screenshot");
		}
		application.run();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Application failed: " << error.what() << '\n';
		return 1;
	}
}
}
