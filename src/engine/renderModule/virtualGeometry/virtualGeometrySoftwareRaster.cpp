#include "virtualGeometrySoftwareRaster.h"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <limits>

#include "renderModule/render.h"
#include "resourceModule/shaderManager.h"
#include "virtualGeometryModule/vmeshFormat.h"

namespace IKIGAI::VG {
namespace {
	constexpr uint32_t TailDraws = 64;
	constexpr uint32_t WorkGroupSize = 64;
	constexpr uint32_t MaxWidth = 1920;
	constexpr uint32_t MaxHeight = 1080;
	constexpr uint32_t DrawIndexLimit = 1u << 25;
	constexpr size_t ProjectedStride = 48;
	constexpr size_t ProjectedCount = TailDraws * VMeshMaxClusterTriangles;
	static_assert(VMeshMaxClusterTriangles == 128);

	struct Parameters {
		std::array<uint32_t, 4> Draw {};
		std::array<uint32_t, 4> Extent {};
	};
	static_assert(sizeof(Parameters) == 32);
}

bool VirtualGeometrySoftwareRaster::ensureShaders() {
	if (!mShaderAttempted) {
		mShaderAttempted = true;
		try {
			mClassify = RESOURCES::ShaderLoader::Create("/shaders/virtualGeometry/vgSoftwareClassify.shader");
			mRaster = RESOURCES::ShaderLoader::Create("/shaders/virtualGeometry/vgSoftwareRaster.shader");
			mMerge = RESOURCES::ShaderLoader::Create("/shaders/virtualGeometry/vgSoftwareMerge.shader");
		} catch (const std::exception& error) {
			std::cerr << "VG_SOFTWARE_SHADER_FALLBACK " << error.what() << '\n';
		}
	}
	return mClassify && mRaster && mMerge;
}

bool VirtualGeometrySoftwareRaster::ensureBuffers(RENDER::Renderer& renderer, uint32_t pixels) {
	using RENDER::BufferUsage;
	try {
		if (!mProjected) {
			mProjected = renderer.createStorageBuffer(nullptr, ProjectedCount, ProjectedStride, BufferUsage::STORAGE);
		}
		if (!mCount) {
			mCount = renderer.createStorageBuffer(
				nullptr, 1, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST | BufferUsage::INDIRECT);
		}
		if (!mParameters) {
			mParameters = renderer.createUniformBuffer(Parameters {});
		}
		if (pixels > mPixelCapacity) {
			auto keys = renderer.createStorageBuffer(nullptr, pixels, sizeof(uint64_t), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST);
			if (!keys) {
				return false;
			}
			mKeys = std::move(keys);
			mPixelCapacity = pixels;
		}
	} catch (const std::exception& error) {
		std::cerr << "VG_SOFTWARE_SCRATCH_FALLBACK " << error.what() << '\n';
		return false;
	}
	return mProjected && mCount && mParameters && mKeys;
}

bool VirtualGeometrySoftwareRaster::prepare(RENDER::Renderer& renderer, const Input& input) {
	mWidth = 0;
	mHeight = 0;
	const auto features = renderer.gpuFeatures();
	if (RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::VULKAN || !features.Compute || !features.BufferInt64Atomics
		|| !features.BufferUsageFlags || !features.BufferTransfer || !features.DrawIndirectCount || !input.Width || !input.Height
		|| input.Width > MaxWidth || input.Height > MaxHeight || !input.DrawCount || input.DrawCount > features.MaxDrawIndirectCount
		|| input.FirstDraw >= DrawIndexLimit || input.DrawCount > DrawIndexLimit - input.FirstDraw || !input.Vertices || !input.Indices
		|| !input.Clusters || !input.DrawRecords || !input.Instances || !input.EngineUbo) {
		return false;
	}
	const uint32_t tailCount = std::min(input.DrawCount, TailDraws);
	const uint32_t tailStart = input.DrawCount - tailCount;
	const uint32_t rasterGroups = tailCount * VMeshMaxClusterTriangles / WorkGroupSize;
	const uint32_t pixels = input.Width * input.Height;
	if (rasterGroups > features.MaxComputeWorkGroupCountX || !ensureShaders() || !ensureBuffers(renderer, pixels)) {
		return false;
	}

	renderer.updateUniformBuffer(mParameters, Parameters { { input.FirstDraw, tailStart, tailCount, 0 }, { input.Width, input.Height, 0, 0 } });
	renderer.fillBuffer(mCount, 0, sizeof(uint32_t), tailStart);
	renderer.fillBuffer(mKeys, 0, size_t(pixels) * sizeof(uint64_t), std::numeric_limits<uint32_t>::max());

	renderer.setShader(mClassify);
	renderer.setUniformBuffer(0, input.EngineUbo);
	renderer.setUniformBuffer(1, mParameters);
	renderer.setStorageBuffer(2, input.Vertices);
	renderer.setStorageBuffer(3, input.Indices);
	renderer.setStorageBuffer(4, input.Clusters);
	renderer.setStorageBuffer(5, input.DrawRecords);
	renderer.setStorageBuffer(6, input.Instances);
	renderer.setStorageBuffer(7, mProjected);
	renderer.setStorageBuffer(8, mCount);
	renderer.dispatch(tailCount, 1, 1);

	renderer.setShader(mRaster);
	renderer.setUniformBuffer(1, mParameters);
	renderer.setStorageBuffer(7, mProjected);
	renderer.setStorageBuffer(8, mCount);
	renderer.setStorageBuffer(9, mKeys);
	renderer.dispatch(rasterGroups, 1, 1);
	mWidth = input.Width;
	mHeight = input.Height;
	return true;
}

void VirtualGeometrySoftwareRaster::merge(RENDER::Renderer& renderer) {
	if (!mWidth) {
		return;
	}
	renderer.setViewport({ { 0, 0 }, { float(mWidth), float(mHeight) } });
	renderer.resetScissor();
	renderer.resetBlending();
	renderer.resetStencil();
	renderer.setCull(RENDER::CullFace::NONE);
	renderer.setDepth({ RENDER::DepthFunction::LESS, true });
	renderer.setPrimitiveMode(RENDER::PrimitiveMode::TRIANGLES);
	renderer.setShader(mMerge);
	renderer.setUniformBuffer(1, mParameters);
	renderer.setStorageBuffer(9, mKeys);
	renderer.draw(3, 0, 1);
}

std::shared_ptr<RENDER::StorageBufferInterface> VirtualGeometrySoftwareRaster::countBuffer() const {
	return mCount;
}

uint64_t VirtualGeometrySoftwareRaster::gpuBytes() const {
	return (mProjected ? ProjectedCount * ProjectedStride : 0) + (mCount ? sizeof(uint32_t) : 0) + (mParameters ? sizeof(Parameters) : 0)
		+ uint64_t(mPixelCapacity) * sizeof(uint64_t);
}
}
