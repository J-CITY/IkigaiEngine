#include "screenshot.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../3rd/stb/stb_image_write.h"

namespace IKIGAI::CAPTURE {
namespace {
	uint8_t byte(float value) {
		if (!std::isfinite(value))
			throw std::runtime_error("Non-finite screenshot pixels");
		return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
	}
	std::filesystem::path automaticPath() {
		static uint64_t sequence = 0;
		const auto now = std::chrono::system_clock::now();
		const auto clock = std::chrono::system_clock::to_time_t(now);
		std::tm calendar { };
#ifdef _WIN32
		localtime_s(&calendar, &clock);
#else
		localtime_r(&clock, &calendar);
#endif
		std::ostringstream name;
		name << "screenshot_" << std::put_time(&calendar, "%Y%m%d_%H%M%S") << '_'
			 << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000 << '_' << sequence++ << ".png";
		return std::filesystem::path("screenshots") / name.str();
	}
	void writeExclusive(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
#ifdef _WIN32
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			throw std::runtime_error("Cannot create PNG (existing file or unavailable path), Win32 error " + std::to_string(GetLastError()));
		bool ok = true;
		for (size_t offset = 0; offset < bytes.size();) {
			DWORD written = 0;
			const DWORD count = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
			if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || written == 0) {
				ok = false;
				break;
			}
			offset += written;
		}
		if (!CloseHandle(file))
			ok = false;
#else
		const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (file < 0)
			throw std::runtime_error("Cannot create PNG (existing file or unavailable path), errno " + std::to_string(errno));
		bool ok = true;
		for (size_t offset = 0; offset < bytes.size();) {
			const auto written = write(file, bytes.data() + offset, std::min<size_t>(bytes.size() - offset, 1024 * 1024));
			if (written < 0 && errno == EINTR)
				continue;
			if (written <= 0) {
				ok = false;
				break;
			}
			offset += static_cast<size_t>(written);
		}
		if (close(file) != 0)
			ok = false;
#endif
		if (!ok) {
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
			throw std::runtime_error("Cannot finish writing PNG");
		}
	}
	ScreenshotService::Result save(ScreenshotImage image, std::filesystem::path path) {
		try {
			if (image.rgba.size() != ScreenshotImage::rgbaSize(image.width, image.height)) {
				throw std::runtime_error("Invalid screenshot buffer");
			}
			std::vector<uint8_t> encoded;
			struct Output {
				std::vector<uint8_t>& bytes;
				bool failed = false;
			} output { encoded };
			const auto append = [](void* context, void* data, int size) {
				auto& out = *static_cast<Output*>(context);
				if (out.failed)
					return;
				try {
					const auto* p = static_cast<const uint8_t*>(data);
					out.bytes.insert(out.bytes.end(), p, p + size);
				} catch (...) {
					out.failed = true;
				}
			};
			if (!stbi_write_png_to_func(append, &output, image.width, image.height, 4, image.rgba.data(), image.width * 4) || output.failed)
				throw std::runtime_error("PNG encoding failed");
			std::filesystem::create_directories(path.parent_path());
			writeExclusive(path, encoded);
			return { ScreenshotService::State::SAVED, std::move(path), { } };
		} catch (const std::exception& e) {
			return { ScreenshotService::State::FAILED, std::move(path), e.what() };
		}
	}
}

std::size_t ScreenshotImage::rgbaSize(int width, int height) {
	if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4
		|| size_t(width) > std::numeric_limits<size_t>::max() / 4 / size_t(height)) {
		throw std::runtime_error("Invalid screenshot dimensions");
	}
	return size_t(width) * size_t(height) * 4;
}

ScreenshotImage ScreenshotImage::fromFloats(int width, int height, const std::vector<float>& pixels, bool flipY, bool bgra) {
	ScreenshotImage image { width, height, std::vector<uint8_t>(rgbaSize(width, height)) };
	if (pixels.size() != image.rgba.size())
		throw std::runtime_error("Screenshot requires RGBA pixels");
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const size_t src = (size_t(flipY ? height - y - 1 : y) * width + x) * 4;
			const size_t dst = (size_t(y) * width + x) * 4;
			image.rgba[dst] = byte(pixels[src + (bgra ? 2 : 0)]);
			image.rgba[dst + 1] = byte(pixels[src + 1]);
			image.rgba[dst + 2] = byte(pixels[src + (bgra ? 0 : 2)]);
			image.rgba[dst + 3] = 255; // Window screenshots are opaque.
		}
	return image;
}
ScreenshotService& ScreenshotService::Get() {
	static ScreenshotService service;
	return service;
}
ScreenshotService::~ScreenshotService() {
	if (mWriter.valid())
		mWriter.wait();
}
void ScreenshotService::poll() {
	if (mWriter.valid() && mWriter.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		try {
			mResult = mWriter.get();
		} catch (const std::exception& e) {
			fail(e.what());
		}
		if (mResult.state == State::SAVED) {
			const auto utf8 = mResult.path.u8string();
			std::cout << "SCREENSHOT_SAVED " << std::string(utf8.begin(), utf8.end()) << std::endl;
		}
		else
			std::cerr << "SCREENSHOT_ERROR " << mResult.error << std::endl;
	}
}
const ScreenshotService::Result& ScreenshotService::status() {
	poll();
	return mResult;
}
bool ScreenshotService::busy() {
	poll();
	return pending() || mResult.state == State::SAVING;
}
bool ScreenshotService::request(const std::filesystem::path& path, const std::string& texture) {
	if (busy())
		return false;
	try {
		auto output = path.empty() ? automaticPath() : path;
		if (output.extension() != ".png")
			throw std::runtime_error("Screenshot output must have a .png extension");
		mResult = { State::PENDING, std::filesystem::absolute(output), { } };
		mTextureName = texture;
		return true;
	} catch (const std::exception& e) {
		fail(e.what());
		return false;
	}
}
void ScreenshotService::fail(const std::string& error) {
	mResult.state = State::FAILED;
	mResult.error = error;
	std::cerr << "SCREENSHOT_ERROR " << error << std::endl;
}
void ScreenshotService::capture(ScreenshotImage image) {
	if (!pending()) {
		return;
	}
	try {
		mResult.state = State::SAVING;
#ifdef __EMSCRIPTEN__
		mResult = save(std::move(image), mResult.path);
#else
		mWriter = std::async(std::launch::async, save, std::move(image), mResult.path);
#endif
	} catch (const std::exception& e) {
		fail(e.what());
	}
}
void ScreenshotService::wait() {
	if (mWriter.valid()) {
		mWriter.wait();
		poll();
	}
	if (pending())
		fail("No frame was available for the screenshot");
}
}
