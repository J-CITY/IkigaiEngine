#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace IKIGAI::CAPTURE {
struct ScreenshotImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
	static std::size_t rgbaSize(int width, int height);
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
};
}
