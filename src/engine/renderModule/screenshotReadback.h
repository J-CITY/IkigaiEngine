#pragma once

#include <functional>
#include <memory>
#include <string>

namespace IKIGAI::CAPTURE {
struct ScreenshotImage;
}

namespace IKIGAI::RENDER {
class DriverInterface;
class TextureInterface;

// Holds pipeline outputs until the backend reaches its screenshot readback point.
class ScreenshotReadback {
public:
	bool hasSource() const;
	void setSource(std::shared_ptr<TextureInterface> scene, std::shared_ptr<TextureInterface> overlay = {});
	void reset();
	void fail(const std::string& error);
	void capture(DriverInterface& driver, const std::function<CAPTURE::ScreenshotImage()>& readBackbuffer);
	void captureBackbuffer(DriverInterface& driver, const std::shared_ptr<TextureInterface>& backbuffer, bool bgra = false);

private:
	std::shared_ptr<TextureInterface> mSource;
	std::shared_ptr<TextureInterface> mOverlay;
};
}
