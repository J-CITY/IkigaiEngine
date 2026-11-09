#include "pipelineCacheStorage.h"

#include "coreModule/config.h"
#include "resourceModule/fileSystem/storage.h"

#include <algorithm>
#include <stdexcept>

namespace IKIGAI::RENDER::PIPELINE_CACHE {
namespace {
	namespace Storage = RESOURCES::STORAGE;
	constexpr std::string_view Magic = "IKPSO001";
	constexpr std::size_t DigestBytes = 64;
	constexpr std::size_t HeaderBytes = Magic.size() + DigestBytes;

	std::filesystem::path CachePath(Backend backend, std::string_view deviceKey) {
		if (deviceKey.empty() || deviceKey.size() > 128 || !std::ranges::all_of(deviceKey, [](char c) {
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
			})) {
			throw std::invalid_argument("Invalid pipeline cache device key");
		}
		const auto root = Config::STORAGE_ROOT.empty() ? Storage::AppDataPath(Config::ORGANIZATION, Config::APPLICATION)
													   : std::filesystem::u8path(Config::STORAGE_ROOT);
		const auto name = backend == Backend::DirectX12 ? "dx12-" : "vulkan-";
		return Storage::Child(root, "cache/pso/" + std::string(name) + std::string(deviceKey) + ".bin");
	}
}

std::vector<uint8_t> Read(Backend backend, std::string_view deviceKey) noexcept {
	try {
		auto file = Storage::Read(CachePath(backend, deviceKey), MaxBlobBytes + HeaderBytes);
		if (file.size() <= HeaderBytes || !std::equal(Magic.begin(), Magic.end(), file.begin())) {
			return {};
		}
		const auto payload = std::span<const uint8_t>(file).subspan(HeaderBytes);
		const std::string_view digest(reinterpret_cast<const char*>(file.data() + Magic.size()), DigestBytes);
		if (Storage::Sha256(payload) != digest) {
			return {};
		}
		file.erase(file.begin(), file.begin() + HeaderBytes);
		return file;
	} catch (...) {
		return {};
	}
}

bool Write(Backend backend, std::string_view deviceKey, std::span<const uint8_t> bytes) noexcept {
	if (bytes.empty() || bytes.size() > MaxBlobBytes) {
		return false;
	}
	try {
		const auto path = CachePath(backend, deviceKey);
		const auto digest = Storage::Sha256(bytes);
		std::vector<uint8_t> file;
		file.reserve(HeaderBytes + bytes.size());
		file.insert(file.end(), Magic.begin(), Magic.end());
		file.insert(file.end(), digest.begin(), digest.end());
		file.insert(file.end(), bytes.begin(), bytes.end());
		Storage::WriteAtomic(path, file);
		return true;
	} catch (...) {
		return false;
	}
}
}
