#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

#ifndef USE_CAPTURE
#define USE_CAPTURE 1
#endif

namespace IKIGAI::CAPTURE {
struct ScreenshotImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
#if USE_CAPTURE
	static std::size_t rgbaSize(int width, int height);
	static ScreenshotImage fromFloats(int width, int height, const std::vector<float>& pixels, bool flipY = false, bool bgra = false);
#else
	static std::size_t rgbaSize(int width, int height) {
		return width > 0 && height > 0 ? std::size_t(width) * std::size_t(height) * 4 : 0;
	}
	static ScreenshotImage fromFloats(int, int, const std::vector<float>&, bool = false, bool = false) {
		return {};
	}
#endif
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
#if USE_CAPTURE
	static ScreenshotService& Get();
	~ScreenshotService();
	bool request(const std::filesystem::path& path = {}, const std::string& texture = {});
	bool pending() const {
		return mResult.state == State::PENDING;
	}
	bool busy();
	const Result& status();
	const std::string& textureName() const {
		return mTextureName;
	}
	void capture(ScreenshotImage image);
	void fail(const std::string& error);
	void wait();

private:
	ScreenshotService() = default;
	void poll();
	Result mResult;
	std::string mTextureName;
	std::future<Result> mWriter;
#else
	static ScreenshotService& Get() {
		static ScreenshotService service;
		return service;
	}
	bool request(const std::filesystem::path& = {}, const std::string& = {}) {
		return false;
	}
	bool pending() const {
		return false;
	}
	bool busy() {
		return false;
	}
	const Result& status() {
		return mResult;
	}
	const std::string& textureName() const {
		return mTextureName;
	}
	void capture(ScreenshotImage) { }
	void fail(const std::string&) { }
	void wait() { }

private:
	Result mResult;
	std::string mTextureName;
#endif
};
}
