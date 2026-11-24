#include "captureModule/screenshot.h"
#include "coreModule/config.h"
#include "coreModule/core/app.h"
#include "renderModule/backends/driverFactory.h"
#include "renderModule/gameRenderer.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/stdLoader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace IKIGAI;
constexpr int width = 800;
constexpr int height = 600;

struct Region {
	int left;
	int top;
	int right;
	int bottom;
};

template <class Predicate> double coverage(Region region, Predicate matches) {
	int count = 0;
	for (int y = region.top; y < region.bottom; ++y) {
		for (int x = region.left; x < region.right; ++x) {
			count += matches(x, y);
		}
	}
	return double(count) / ((region.right - region.left) * (region.bottom - region.top));
}

struct Frame {
	const unsigned char* rgb;
	CORE::Core& core;
};

void checkCoverage(const char* name, double value, double minimum, double maximum = 1.0) {
	std::cout << name << " coverage=" << value << std::endl;
	if (!(value >= minimum && value <= maximum)) {
		throw std::runtime_error(std::string(name) + " outside expected range [" + std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
	}
}

void checkGui(Frame frame) {
	const auto ribbon = [&](int x, int y) {
		const auto* color = frame.rgb + (y * width + x) * 3;
		return color[0] > 210 && color[1] > 85 && color[1] < 195 && color[2] < 125;
	};
	checkCoverage("orange ribbon inside clip", coverage({ 424, 371, 742, 443 }, ribbon), 0.07);
	checkCoverage("ribbon clipped at right edge", coverage({ 764, 426, 790, 450 }, ribbon), 0, 0.02);
}

void checkEnvironment(Frame frame) {
	const auto& graph = frame.core.renderer->getCurrentPipeline();
	const auto texture = graph.mTextures.at("Depth");
	if (graph.mAliasedTextures.contains("Depth") || texture->getWidth() != width || texture->getHeight() != height) {
		throw std::runtime_error("Expected a persistent display-sized depth texture");
	}
	const auto depth = frame.core.driver->readTexture(texture, 0, 0);
	if (depth.size() != width * height || !std::all_of(depth.begin(), depth.end(), [](float d) { return std::isfinite(d) && d >= 0 && d <= 1; })) {
		throw std::runtime_error("Invalid depth readback");
	}
	const auto hasGeometry = [&](int x, int y) { return depth[(height - 1 - y) * width + x] < 0.9999f; };
	const Region ground { 490, 460, 730, 555 };
	checkCoverage("landscape depth", coverage(ground, hasGeometry), 0.80);
	checkCoverage("clear sky depth", coverage({ 80, 30, 310, 100 }, hasGeometry), 0, 0.15);
	checkCoverage("visible landscape",
		coverage(ground,
			[&](int x, int y) {
				const auto* color = frame.rgb + (y * width + x) * 3;
				return color[1] > 25 && color[1] > color[2] + 6 && color[1] > color[0] + 6;
			}),
		0.45);
}

struct Scenario {
	const char* name;
	const char* pipeline;
	void (*check)(Frame);
};
constexpr Scenario scenarios[] = {
	{ "gui-demo", "/renderPipeline/vector-sdf.json", checkGui },
	{ "environment-demo", "/renderPipeline/environment-demo.json", checkEnvironment },
};

void run(const Scenario& scenario) {
	Config::WINDOW_WIDTH = width;
	Config::WINDOW_HEIGHT = height;
	Config::WINDOW_FULLSCREEN = false;
	Config::STORAGE_ROOT = std::filesystem::absolute("storage").string();
	RENDER::SetCliRenderBackendOverride("opengl");
	RENDER::SetCliRenderExecutorOverride("immediate");
	const auto scene = std::string("scenes/") + scenario.name + ".json";
	CORE::App app({}, scene);
	const auto nextFrame = [&] {
		if (!app.runMainLoop()) {
			throw std::runtime_error("Window closed before capture");
		}
	};
	auto& core = app.getCore().get();
	auto load = core.sceneManager->loadFromFileAsync(scene);
	while (!load.done()) {
		nextFrame();
	}
	if (load.status() != SCENE_SYSTEM::SceneLoadStatus::Completed) {
		throw std::runtime_error("Scene load failed: " + load.error());
	}
	for (int i = 0; i < 3; ++i) {
		nextFrame();
	}
	auto& capture = CAPTURE::ScreenshotService::Get();
	if (!capture.request()) {
		throw std::runtime_error("Cannot request frame.png");
	}
	nextFrame();
	capture.wait();
	if (capture.status().state != CAPTURE::ScreenshotService::State::SAVED) {
		throw std::runtime_error("Screenshot failed: " + capture.status().error);
	}
	std::filesystem::rename(capture.status().path, "frame.png");
	const auto& actualPipeline = core.renderer->getCurrentPipeline().mPipelineAsset;
	if (actualPipeline != scenario.pipeline) {
		throw std::runtime_error("Unexpected pipeline: " + actualPipeline);
	}
	int imageWidth = 0;
	int imageHeight = 0;
	int channels = 0;
	UTILS::STBiSetFlipVerticallyOnLoad(false);
	std::unique_ptr<unsigned char, void (*)(unsigned char*)> pixels(
		UTILS::STBiLoad("frame.png", &imageWidth, &imageHeight, &channels, 3), UTILS::STBiImageFree);
	if (!pixels || imageWidth != width || imageHeight != height) {
		throw std::runtime_error("Expected an 800x600 frame.png");
	}
	scenario.check({ pixels.get(), core });
}
}

int main(int argc, char** argv) {
	try {
		for (const auto& scenario : scenarios) {
			if (argc == 2 && std::string_view(argv[1]) == scenario.name) {
				run(scenario);
				return 0;
			}
		}
		throw std::invalid_argument("Expected a registered render scene name");
	} catch (const std::exception& error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
