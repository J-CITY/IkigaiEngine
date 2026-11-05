#include "gpuScene.h"

#if defined(IKIGAI_USE_GPU_DRIVEN)
#include "backends/interface/materialInterface.h"
#include "backends/interface/meshInterface.h"
#include "drawable.h"
#include "frustum.h"
#include "render.h"
#include "resourceModule/shaderManager.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <map>
#include <tuple>

namespace IKIGAI::RENDER {
namespace {
	constexpr uint32_t WorkGroupSize = 64;
	constexpr uint32_t MaxInstancesPerBin = 65536;
	constexpr const char* CullShader = "/shaders/render/ordinary/cull.shader";

	struct alignas(16) InstanceGpu {
		InstanceTransform Transform;
		MATH::Vector4f ModelBounds = { 0, 0, 0, -2 };
		MATH::Vector4f MeshBounds = { 0, 0, 0, -1 };
	};
	static_assert(sizeof(InstanceGpu) == 176);

	struct alignas(16) CullView {
		std::array<std::array<float, 4>, 6> Planes {};
		std::array<uint32_t, 4> Counts {};
	};
	static_assert(sizeof(CullView) == 112);

	using BinKey = std::tuple<const MeshInterface*, const MaterialInterface*>;

	struct Row {
		InstanceGpu Data;
		uint64_t LastSeen = 0;
	};

	struct Bin {
		std::shared_ptr<MeshInterface> Mesh;
		std::shared_ptr<MaterialInterface> Material;
		std::map<int, uint32_t> Slots;
		std::vector<Row> Rows;
		std::vector<uint32_t> Free;
		std::vector<uint32_t> Dirty;
		std::shared_ptr<StorageBufferInterface> Input;
		GpuSceneDraw Draw;
		uint32_t Capacity = 0;
	};

	MATH::Vector4f PackBounds(const BoundingSphere& sphere) {
		return { sphere.position.x, sphere.position.y, sphere.position.z, sphere.radius };
	}

	void UploadRows(Renderer& renderer, Bin& bin) {
		const auto rows = uint32_t(bin.Rows.size());
		if (rows > bin.Capacity) {
			bin.Capacity = std::bit_ceil(std::max(rows, 16u));
			bin.Input = renderer.createStorageBuffer(nullptr, bin.Capacity, sizeof(InstanceGpu), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST);
			bin.Draw.Transforms = renderer.createStorageBuffer(nullptr, bin.Capacity, sizeof(InstanceTransform), BufferUsage::STORAGE);
			bin.Dirty.resize(rows);
			for (uint32_t row = 0; row < rows; ++row) {
				bin.Dirty[row] = row;
			}
		}
		std::ranges::sort(bin.Dirty);
		bin.Dirty.erase(std::unique(bin.Dirty.begin(), bin.Dirty.end()), bin.Dirty.end());
		std::vector<InstanceGpu> data;
		for (size_t start = 0; start < bin.Dirty.size();) {
			const uint32_t first = bin.Dirty[start];
			data.clear();
			do {
				data.push_back(bin.Rows[bin.Dirty[start]].Data);
				++start;
			} while (start < bin.Dirty.size() && bin.Dirty[start] == first + data.size());
			renderer.updateStorageBufferRange(bin.Input, data.data(), data.size() * sizeof(InstanceGpu), first * sizeof(InstanceGpu));
		}
		bin.Dirty.clear();
	}
}

struct GpuScene::State {
	std::shared_ptr<ShaderInterface> Shader;
	bool ShaderAttempted = false;
	uint64_t ShaderGeneration = UINT64_MAX;
	std::array<size_t, 4> Bindings {};
	std::shared_ptr<UniformBufferInterface> ViewBuffer;
	uint64_t Frame = 0;
	std::map<BinKey, Bin> Bins;
	std::vector<Draw> Draws;
};

GpuScene::GpuScene()
	: mState(std::make_unique<State>()) { }
GpuScene::~GpuScene() = default;

bool GpuScene::available(Renderer& renderer) {
	if (!mState->ShaderAttempted) {
		mState->ShaderAttempted = true;
		const auto features = renderer.gpuFeatures();
		if (!features.Compute || !features.BufferUsageFlags || !features.BufferTransfer || !features.DrawIndirect
			|| features.MaxComputeWorkGroupCountX < MaxInstancesPerBin / WorkGroupSize) {
			std::cout << "GPU_SCENE_FALLBACK compute, buffer upload or indexed indirect unsupported\n";
			return false;
		}
		try {
			mState->Shader = RESOURCES::ShaderLoader::Create(CullShader);
			if (!mState->Shader || !mState->Shader->mShaderPaths.contains(ShaderType::COMPUTE)) {
				throw std::runtime_error("Missing ordinary culling compute shader");
			}
		} catch (const std::exception& error) {
			std::cerr << "GPU_SCENE_FALLBACK shader: " << error.what() << '\n';
			mState->Shader.reset();
		}
	}
	if (mState->Shader && mState->ShaderGeneration != mState->Shader->generation()) {
		const auto& reflection = mState->Shader->getReflection();
		constexpr std::array names { "Candidates", "Visible", "Arguments", "Cull" };
		for (size_t index = 0; index < names.size(); ++index) {
			const auto id = reflection.mNameToUniforms.identifier(names[index]);
			if (!reflection.mNameToUniforms.contains(id)) {
				std::cerr << "GPU_SCENE_FALLBACK missing shader binding " << names[index] << '\n';
				mState->Shader.reset();
				return false;
			}
			const auto& uniform = reflection.mUniforms.at(reflection.mNameToUniforms.at(id));
			using Type = ShaderReflection::UniformType;
			const auto expected = index + 1 == names.size() ? Type::UNIFORM_BUFFER : Type::STORAGE_BUFFER;
			if (uniform.mType != expected) {
				std::cerr << "GPU_SCENE_FALLBACK unexpected binding type for " << names[index] << '\n';
				mState->Shader.reset();
				return false;
			}
			mState->Bindings[index] = uniform.mBind;
		}
		mState->ShaderGeneration = mState->Shader->generation();
	}
	return bool(mState->Shader);
}

void GpuScene::prepare(Renderer& renderer, std::span<const GpuSceneInstance> instances, const Frustum* frustum) {
	auto& state = *mState;
	++state.Frame;
	state.Draws.clear();
	for (const auto& instance : instances) {
		auto& bin = state.Bins[{ instance.Mesh.get(), instance.Material.get() }];
		bin.Mesh = instance.Mesh;
		bin.Material = instance.Material;
		auto slot = bin.Slots.find(instance.Owner);
		if (slot == bin.Slots.end()) {
			if (bin.Rows.size() == MaxInstancesPerBin && bin.Free.empty()) {
				continue;
			}
			uint32_t index = uint32_t(bin.Rows.size());
			if (bin.Free.empty()) {
				bin.Rows.emplace_back();
			}
			else {
				index = bin.Free.back();
				bin.Free.pop_back();
			}
			slot = bin.Slots.emplace(instance.Owner, index).first;
		}
		auto& row = bin.Rows[slot->second];
		row.LastSeen = state.Frame;
		const auto& transform = instance.Transform;
		const InstanceGpu data { { MATH::Matrix4f::Transpose(transform.World), MATH::Matrix4f::Transpose(transform.PreviousWorld), transform.Tint },
			PackBounds(instance.ModelBounds), PackBounds(instance.MeshBounds) };
		if (std::memcmp(&row.Data, &data, sizeof(data)) != 0) {
			row.Data = data;
			bin.Dirty.push_back(slot->second);
		}
	}

	CullView view;
	if (frustum) {
		for (size_t plane = 0; plane < view.Planes.size(); ++plane) {
			view.Planes[plane] = frustum->getPlane(plane);
		}
	}
	else {
		for (auto& plane : view.Planes) {
			plane[3] = 1.f;
		}
	}
	for (auto entry = state.Bins.begin(); entry != state.Bins.end();) {
		auto& bin = entry->second;
		for (auto slot = bin.Slots.begin(); slot != bin.Slots.end();) {
			auto& row = bin.Rows[slot->second];
			if (row.LastSeen == state.Frame) {
				++slot;
				continue;
			}
			row.Data.ModelBounds.w = -2;
			bin.Dirty.push_back(slot->second);
			bin.Free.push_back(slot->second);
			slot = bin.Slots.erase(slot);
		}
		if (bin.Slots.empty()) {
			entry = state.Bins.erase(entry);
			continue;
		}
		++entry;
		UploadRows(renderer, bin);
		const DrawIndexedIndirectCommand args { uint32_t(bin.Mesh->getIndexCount()), 0, 0, 0, 0 };
		if (!bin.Draw.Arguments) {
			bin.Draw.Arguments
				= renderer.createStorageBuffer(nullptr, 1, sizeof(args), BufferUsage::STORAGE | BufferUsage::INDIRECT | BufferUsage::TRANSFER_DST);
		}
		renderer.updateStorageBufferRange(bin.Draw.Arguments, &args, sizeof(args), 0);
		renderer.setShader(state.Shader);
		renderer.setStorageBuffer(state.Bindings[0], bin.Input);
		renderer.setStorageBuffer(state.Bindings[1], bin.Draw.Transforms);
		renderer.setStorageBuffer(state.Bindings[2], bin.Draw.Arguments);
		view.Counts[0] = uint32_t(bin.Rows.size());
		if (!state.ViewBuffer) {
			state.ViewBuffer = renderer.createUniformBuffer(nullptr, sizeof(view));
		}
		renderer.updateUniformBuffer(state.ViewBuffer, view);
		renderer.setUniformBuffer(state.Bindings[3], state.ViewBuffer);
		renderer.dispatch((view.Counts[0] + WorkGroupSize - 1) / WorkGroupSize, 1, 1);
		state.Draws.push_back({ bin.Mesh, bin.Material, bin.Draw });
	}
}

bool GpuScene::contains(const Drawable& drawable) const {
	const auto bin = mState->Bins.find({ drawable.mesh.get(), drawable.material.get() });
	return bin != mState->Bins.end() && bin->second.Slots.contains(drawable.OwnerId);
}

std::span<const GpuScene::Draw> GpuScene::draws() const {
	return mState->Draws;
}
}
#endif
