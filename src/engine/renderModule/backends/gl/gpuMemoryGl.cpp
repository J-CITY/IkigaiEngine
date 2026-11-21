#include "gpuMemoryGl.h"
#if defined(OPENGL_BACKEND) && IKIGAI_ENABLE_MEMORY_TRACKING
#include "renderModule/backends/graphicsWrapper.hpp"
#include "renderModule/backends/interface/driverInterface.h"
#include <algorithm>
#include <limits>

namespace IKIGAI::RENDER {
namespace {
PROFILER::GpuAllocationInfo Info(unsigned id, PROFILER::GpuResourceKind kind, uint64_t bytes, const char* name = nullptr) {
    PROFILER::GpuAllocationInfo info;
    info.backend = PROFILER::GpuBackend::OpenGL;
    info.device = reinterpret_cast<uintptr_t>(DriverInterface::Get());
    info.handle = id; info.kind = kind; info.bytes = bytes; info.requestedBytes = bytes;
    info.measurement = PROFILER::GpuMeasurement::EstimatedStorage;
    info.name = name;
    return info;
}
}
uint32_t TextureBytesPerTexelGl(unsigned format) {
    switch (format) {
    case GL_RED: case GL_R8: return 1;
    case GL_RG: case GL_RG8: case GL_R16F: case GL_DEPTH_COMPONENT16: return 2;
    case GL_RGB: case GL_RGB8: return 3;
    case GL_RGB16F: return 6;
    case GL_R32F: case GL_RG16F: case GL_DEPTH24_STENCIL8: case GL_DEPTH_COMPONENT32F: return 4;
    case GL_RGBA16F: case GL_RG32F: case GL_DEPTH32F_STENCIL8: return 8;
    case GL_RGB32F: return 12;
    case GL_RGBA32F: return 16;
    default: return 4;
    }
}
void TrackBufferMemoryGl(PROFILER::GpuAllocationToken& token, unsigned target, unsigned id, PROFILER::GpuResourceKind kind) {
    GLint64 bytes = 0;
    glGetBufferParameteri64v(target, GL_BUFFER_SIZE, &bytes);
    if (bytes > 0 && id) token.track(Info(id, kind, static_cast<uint64_t>(bytes)));
    else token.reset();
}
void TrackTextureMemoryGl(PROFILER::GpuAllocationToken& token, unsigned target, unsigned id,
    const TextureMemoryLayoutGl& layout, const char* name) {
    uint64_t bytes = 0;
#ifndef USING_GLES
    const auto faces = target == GL_TEXTURE_CUBE_MAP ? 6u : 1u;
    for (uint32_t face = 0; face < faces; ++face) {
        const auto imageTarget = faces == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target;
        for (uint32_t level = 0; level < layout.levels; ++level) {
            GLint width = 0, height = 0, depth = 1, compressed = 0;
            glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_WIDTH, &width);
            if (width <= 0) break;
            glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_HEIGHT, &height);
            if (target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY)
                glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_DEPTH, &depth);
            glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_COMPRESSED, &compressed);
            if (compressed) {
                GLint size = 0; glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
                bytes += static_cast<uint64_t>(std::max(0, size));
            } else {
                GLint bits = 0;
                for (auto component : {GL_TEXTURE_RED_SIZE, GL_TEXTURE_GREEN_SIZE, GL_TEXTURE_BLUE_SIZE,
                        GL_TEXTURE_ALPHA_SIZE, GL_TEXTURE_DEPTH_SIZE, GL_TEXTURE_STENCIL_SIZE}) {
                    GLint value = 0; glGetTexLevelParameteriv(imageTarget, level, component, &value); bits += value;
                }
                GLint format = 0; glGetTexLevelParameteriv(imageTarget, level, GL_TEXTURE_INTERNAL_FORMAT, &format);
                const uint64_t pixelBytes = format == GL_DEPTH32F_STENCIL8 ? 8 : static_cast<uint64_t>((bits + 7) / 8);
                bytes += static_cast<uint64_t>(width) * std::max(1, height) * std::max(1, depth) * pixelBytes;
            }
        }
    }
#else
    // The current GLES TextureGl creation path does not allocate 3D storage.
    if (layout.volume) { token.reset(); return; }
    uint64_t width = layout.width, height = layout.height, depth = layout.depth;
    for (uint32_t level = 0; level < layout.levels; ++level) {
        bytes += width * height * depth * layout.layers * layout.bytesPerTexel;
        width = std::max(uint64_t{1}, width / 2); height = std::max(uint64_t{1}, height / 2);
        if (layout.volume) depth = std::max(uint64_t{1}, depth / 2);
    }
#endif
    if (bytes && id) token.track(Info(id, PROFILER::GpuResourceKind::Texture, bytes, name));
    else token.reset();
}
void TrackRenderbufferMemoryGl(PROFILER::GpuAllocationToken& token, unsigned id) {
    GLint width = 0, height = 0, depth = 0, stencil = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &depth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &stencil);
    const uint64_t bytes = static_cast<uint64_t>(std::max(0, width)) * std::max(0, height) * ((depth + stencil + 7) / 8);
    if (bytes && id) token.track(Info(id, PROFILER::GpuResourceKind::RenderTarget, bytes, "Framebuffer depth"));
    else token.reset();
}
}
#endif
