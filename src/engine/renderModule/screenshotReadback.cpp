#include "screenshotReadback.h"

#include "backends/interface/driverInterface.h"
#include "captureModule/screenshot.h"

#include <stdexcept>
#include <utility>

namespace IKIGAI::RENDER {
namespace {
	CAPTURE::ScreenshotImage readSource(
		DriverInterface& driver, const std::shared_ptr<TextureInterface>& source, const std::shared_ptr<TextureInterface>& overlay) {
		const int width = static_cast<int>(source->getWidth());
		const int height = static_cast<int>(source->getHeight());
		auto pixels = driver.readTexture(source, 0, 0);
		if (overlay) {
			if (overlay->getWidth() != size_t(width) || overlay->getHeight() != size_t(height)) {
				throw std::runtime_error("Screenshot UI size mismatch");
			}
			const auto uiPixels = driver.readTexture(overlay, 0, 0);
			if (pixels.size() != CAPTURE::ScreenshotImage::rgbaSize(width, height) || uiPixels.size() != pixels.size()) {
				throw std::runtime_error("Invalid screenshot composition inputs");
			}
			for (size_t i = 0; i < pixels.size(); i += 4) {
				for (size_t channel = 0; channel < 3; ++channel) {
					pixels[i + channel] = uiPixels[i + channel] + pixels[i + channel] * (1.0f - uiPixels[i + 3]);
				}
			}
		}
		return CAPTURE::ScreenshotImage::fromFloats(width, height, pixels, DriverInterface::settings.backend == RenderSettings::Backend::OPENGL,
			source->getFormat() == PixelFormat::BGRA_INT);
	}
}

bool ScreenshotReadback::hasSource() const {
	return static_cast<bool>(mSource);
}

void ScreenshotReadback::setSource(std::shared_ptr<TextureInterface> scene, std::shared_ptr<TextureInterface> overlay) {
	if (CAPTURE::ScreenshotService::Get().pending()) {
		mSource = std::move(scene);
		mOverlay = std::move(overlay);
	}
}

void ScreenshotReadback::reset() {
	mSource.reset();
	mOverlay.reset();
}

void ScreenshotReadback::fail(const std::string& error) {
	reset();
	CAPTURE::ScreenshotService::Get().fail(error);
}

void ScreenshotReadback::capture(DriverInterface& driver, const std::function<CAPTURE::ScreenshotImage()>& readBackbuffer) {
	auto& screenshots = CAPTURE::ScreenshotService::Get();
	if (!screenshots.pending()) {
		reset();
		return;
	}
	// A named texture is supplied by the first scene frame, after loading frames.
	if (!mSource && !screenshots.textureName().empty()) {
		return;
	}
	try {
		CAPTURE::ScreenshotImage image;
		if (mSource) {
			image = readSource(driver, mSource, mOverlay);
		}
		else {
			image = readBackbuffer();
		}
		reset();
		screenshots.capture(std::move(image));
	} catch (const std::exception& e) {
		fail(e.what());
	}
}

void ScreenshotReadback::captureBackbuffer(DriverInterface& driver, const std::shared_ptr<TextureInterface>& backbuffer, bool bgra) {
	capture(driver, [&] {
		return CAPTURE::ScreenshotImage::fromFloats(
			static_cast<int>(backbuffer->getWidth()), static_cast<int>(backbuffer->getHeight()), driver.readTexture(backbuffer, 0, 0), false, bgra);
	});
}
}
