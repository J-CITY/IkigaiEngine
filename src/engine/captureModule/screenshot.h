#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
class DriverInterface;
class TextureInterface;
}

namespace IKIGAI::CAPTURE {
struct ScreenshotImage {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba;
	static ScreenshotImage fromFloats(int width, int height, const std::vector<float>& pixels, bool flipY = false, bool bgra = false);
};

// Request, capture, status and wait are called on the main/render thread.
// The writer owns only CPU pixels and a filesystem path, never GPU objects.
class ScreenshotService {
public:
	enum class State { IDLE, PENDING, SAVING, SAVED, FAILED };
	struct Result {
		State state = State::IDLE;
		std::filesystem::path path;
		std::string error;
	};
	static ScreenshotService& Get();
	~ScreenshotService();
	bool request(const std::filesystem::path& path = { }, const std::string& texture = { });
	bool pending() const {
		return mResult.state == State::PENDING;
	}
	bool busy();
	bool hasSource() const { return static_cast<bool>(mSource); }
	const Result& status();
	const std::string& textureName() const {
		return mTextureName;
	}
	void setSource(std::shared_ptr<RENDER::TextureInterface> scene, std::shared_ptr<RENDER::TextureInterface> overlay = { });
	void capture(const std::function<ScreenshotImage()>& readWindow);
	void captureTexture(RENDER::DriverInterface& driver, std::shared_ptr<RENDER::TextureInterface> backbuffer, bool bgra = false);
	void fail(const std::string& error);
	void wait();

private:
	ScreenshotService() = default;
	void poll();
	ScreenshotImage readSource(RENDER::DriverInterface& driver);
	Result mResult;
	std::string mTextureName;
	std::shared_ptr<RENDER::TextureInterface> mSource, mOverlay;
	std::future<Result> mWriter;
};
}
