#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace IKIGAI::RENDER::PIPELINE_CACHE {
enum class Backend { DirectX12, Vulkan };

inline constexpr std::size_t MaxBlobBytes = 64 * 1024 * 1024;

std::vector<uint8_t> Read(Backend backend, std::string_view deviceKey) noexcept;
bool Write(Backend backend, std::string_view deviceKey, std::span<const uint8_t> bytes) noexcept;
}
