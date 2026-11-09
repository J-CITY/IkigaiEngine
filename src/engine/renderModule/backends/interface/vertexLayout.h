#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {

enum class VertexInputRate { PerVertex, PerInstance };
enum class VertexFormat {
    Float, Float2, Float3, Float4,
    Int, Int2, Int3, Int4,
    UInt, UInt2, UInt3, UInt4,
    UNorm8x4, SNorm8x4
};
enum class VertexScalarType { Float, Int, UInt };
struct VertexFormatInfo {
    uint32_t components, bytes;
    VertexScalarType shaderType;
    bool normalized = false;
};
inline VertexFormatInfo GetVertexFormatInfo(VertexFormat format) {
    const auto value = static_cast<uint32_t>(format);
    if (value < 12) return {value % 4 + 1, (value % 4 + 1) * 4,
        value < 4 ? VertexScalarType::Float : value < 8 ? VertexScalarType::Int : VertexScalarType::UInt};
    if (format == VertexFormat::UNorm8x4 || format == VertexFormat::SNorm8x4)
        return {4, 4, VertexScalarType::Float, true};
    throw std::invalid_argument("Invalid vertex format");
}
struct VertexStreamLayout {
	uint32_t binding = 0;
	uint32_t stride = 0;
	VertexInputRate rate = VertexInputRate::PerVertex;
	uint32_t stepRate = 1;
	auto operator<=>(const VertexStreamLayout&) const = default;
};
struct VertexInputAttribute {
	uint32_t location = 0;
	uint32_t binding = 0;
	VertexFormat format = VertexFormat::Float3;
	uint32_t offset = 0;
	auto operator<=>(const VertexInputAttribute&) const = default;
};
struct VertexLayout {
	std::vector<VertexStreamLayout> streams;
	std::vector<VertexInputAttribute> attributes;
	bool operator==(const VertexLayout&) const = default;
    const VertexStreamLayout& stream(uint32_t binding) const {
        for (const auto& s : streams) if (s.binding == binding) return s;
        throw std::invalid_argument("Vertex attribute references a missing stream");
    }
    void validate() const {
        for (size_t i = 0; i < streams.size(); ++i) {
            const auto& s = streams[i];
            if (!s.stride || s.binding >= 16 || !s.stepRate)
                throw std::invalid_argument("Invalid vertex stream stride, binding or step rate");
            if (s.rate!=VertexInputRate::PerVertex && s.rate!=VertexInputRate::PerInstance)
                throw std::invalid_argument("Invalid vertex input rate");
            // Step > 1 needs additional Vulkan divisor features; reject consistently.
            if (s.stepRate != 1) throw std::invalid_argument("Vertex instance step rate must be 1");
            for (size_t j = 0; j < i; ++j) if (streams[j].binding == s.binding)
                throw std::invalid_argument("Duplicate vertex stream binding");
        }
        for (size_t i = 0; i < attributes.size(); ++i) {
            const auto& a = attributes[i];
            const auto info = GetVertexFormatInfo(a.format);
            const auto& s = stream(a.binding);
            if (a.location >= 16 || a.offset > s.stride || info.bytes > s.stride - a.offset)
                throw std::invalid_argument("Vertex attribute exceeds stream stride or location limit");
            if (!info.normalized && (a.offset%4 || s.stride%4))
                throw std::invalid_argument("32-bit vertex attributes require four-byte offset/stride alignment");
            for (size_t j = 0; j < i; ++j) if (attributes[j].location == a.location)
                throw std::invalid_argument("Duplicate vertex attribute location");
        }
    }
    // Full structural key avoids collisions in pipeline/VAO caches.
    std::string key() const {
        std::string result;
        for (const auto& s : streams) result += "s" + std::to_string(s.binding) + ":" +
            std::to_string(s.stride) + ":" + std::to_string(static_cast<int>(s.rate)) + ":" + std::to_string(s.stepRate) + ";";
        for (const auto& a : attributes) result += "a" + std::to_string(a.location) + ":" +
            std::to_string(a.binding) + ":" + std::to_string(static_cast<int>(a.format)) + ":" + std::to_string(a.offset) + ";";
        return result;
    }
};
struct VertexStreamData {
    uint32_t binding = 0;
    std::span<const std::byte> bytes;
    size_t count = 0;
    template<class T> static VertexStreamData From(uint32_t binding, const std::vector<T>& values) {
        return {binding, std::as_bytes(std::span(values)), values.size()};
    }
};
struct DrawArguments {
    uint32_t count = 0;
    uint32_t first = 0;
    uint32_t instanceCount = 1;
    int32_t baseVertex = 0;
    uint32_t firstInstance = 0;
    bool indexed = false;
};
}
