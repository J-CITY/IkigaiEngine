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
#include <set>
#include <tuple>

namespace IKIGAI::RENDER {
namespace {
	constexpr uint32_t WorkGroupSize = 64;
	constexpr uint32_t MaxInstancesPerBin = 65536;
	constexpr const char* CullShader = "/shaders/render/ordinary/cull.shader";
	constexpr const char* LodShader = "/shaders/render/ordinary/selectLod.shader";

	struct alignas(16) InstanceGpu {
		InstanceTransform Transform;
		MATH::Vector4f ModelBounds = { 0, 0, 0, -2 };
		MATH::Vector4f MeshBounds = { 0, 0, 0, -1 };
		std::array<uint32_t, 4> Lod { UINT32_MAX, UINT32_MAX, 0, 0 };
		MATH::Vector4f LocalBounds {};
	};
	static_assert(sizeof(InstanceGpu) == 208);

	enum class CullPhase : uint32_t { Frustum, PreviousHzb, CurrentHzb, Shadow, Recover };

	struct alignas(16) CullView {
		std::array<std::array<float, 4>, 6> Planes {};
		std::array<uint32_t, 4> Counts {};
		std::array<std::array<float, 4>, 4> OcclusionViewProjection {};
	};
	static_assert(sizeof(CullView) == 176);

	using BinKey = std::tuple<const MeshInterface*, const MaterialInterface*>;
	using SlotKey = std::pair<int, uint32_t>;

	template <typename T> struct Row {
		T Data;
		uint64_t LastSeen = 0;
	};

	struct Bin {
		std::shared_ptr<MeshInterface> Mesh;
		std::shared_ptr<MaterialInterface> Material;
		std::map<SlotKey, uint32_t> Slots;
		std::vector<Row<InstanceGpu>> Rows;
		std::vector<uint32_t> Free;
		std::vector<uint32_t> Dirty;
		std::shared_ptr<StorageBufferInterface> Input;
		std::shared_ptr<StorageBufferInterface> Rejected;
		GpuSceneDraw Draw;
		GpuSceneDraw ShadowDraw;
		bool CastsShadow = false;
		uint32_t Capacity = 0;
	};

	MATH::Vector4f PackBounds(const BoundingSphere& sphere) {
		return { sphere.position.x, sphere.position.y, sphere.position.z, sphere.radius };
	}

	void SetCullProjection(CullView& view, const MATH::Matrix4f& projection) {
		for (size_t row = 0; row < 4; ++row) {
			std::copy_n(projection.getData() + 4 * row, 4, view.OcclusionViewProjection[row].begin());
		}
	}

	void SetCullPlanes(CullView& view, const Frustum* frustum) {
		for (size_t plane = 0; plane < view.Planes.size(); ++plane) {
			view.Planes[plane] = frustum ? frustum->getPlane(plane) : std::array<float, 4> { 0, 0, 0, 1 };
		}
	}

	void EnsureDrawBuffers(Renderer& renderer, GpuSceneDraw& draw, uint32_t capacity) {
		if (!draw.Transforms || draw.Transforms->getSizeByte() < capacity * sizeof(InstanceTransform)) {
			draw.Transforms = renderer.createStorageBuffer(nullptr, capacity, sizeof(InstanceTransform), BufferUsage::STORAGE);
		}
		if (!draw.Arguments) {
			draw.Arguments = renderer.createStorageBuffer(
				nullptr, 1, sizeof(DrawIndexedIndirectCommand), BufferUsage::STORAGE | BufferUsage::INDIRECT | BufferUsage::TRANSFER_DST);
		}
	}

	template <typename T>
	void UploadRows(Renderer& renderer, const std::shared_ptr<StorageBufferInterface>& input, const std::vector<Row<T>>& rows,
		std::vector<uint32_t>& dirty, bool resized) {
		if (resized) {
			dirty.resize(rows.size());
			for (uint32_t row = 0; row < rows.size(); ++row) {
				dirty[row] = row;
			}
		}
		std::ranges::sort(dirty);
		dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
		std::vector<T> data;
		for (size_t start = 0; start < dirty.size();) {
			const uint32_t first = dirty[start];
			data.clear();
			do {
				data.push_back(rows[dirty[start]].Data);
				++start;
			} while (start < dirty.size() && dirty[start] == first + data.size());
			renderer.updateStorageBufferRange(input, data.data(), data.size() * sizeof(T), first * sizeof(T));
		}
		dirty.clear();
	}

	struct alignas(16) LodGpu {
		MATH::Vector4f Position {};
		std::array<float, GpuSceneLod::MaxLevels> Distances {};
		std::array<uint32_t, 4> State {};
	};
	static_assert(sizeof(LodGpu) == 48);

	struct alignas(16) LodView {
		MATH::Vector4f Position {};
		std::array<uint32_t, 4> Counts {};
	};
	static_assert(sizeof(LodView) == 32);

	using Type = ShaderReflection::UniformType;

	template <size_t N> struct ComputeShader {
		std::shared_ptr<ShaderInterface> Shader;
		std::array<size_t, N> Bindings {};
		uint64_t Generation = UINT64_MAX;
		bool Attempted = false;

		bool resolve(const char* path, const std::array<std::pair<const char*, Type>, N>& expected) {
			try {
				if (!Attempted) {
					Attempted = true;
					Shader = RESOURCES::ShaderLoader::Create(path);
					if (!Shader || !Shader->mShaderPaths.contains(ShaderType::COMPUTE)) {
						throw std::runtime_error("Missing compute shader");
					}
				}
				if (Shader && Generation != Shader->generation()) {
					const auto& reflection = Shader->getReflection();
					for (size_t index = 0; index < expected.size(); ++index) {
						const auto [name, type] = expected[index];
						const auto id = reflection.mNameToUniforms.identifier(name);
						if (!reflection.mNameToUniforms.contains(id)) {
							throw std::runtime_error(std::string("Missing binding ") + name);
						}
						const auto& uniform = reflection.mUniforms.at(reflection.mNameToUniforms.at(id));
						if (uniform.mType != type) {
							throw std::runtime_error(std::string("Unexpected binding type for ") + name);
						}
						Bindings[index] = uniform.mBind;
					}
					Generation = Shader->generation();
				}
			} catch (const std::exception& error) {
				std::cerr << "GPU_SCENE_FALLBACK " << path << ": " << error.what() << '\n';
				Shader.reset();
			}
			return bool(Shader);
		}
	};

	struct LodTable {
		ComputeShader<3> Select;
		std::vector<Row<LodGpu>> Rows;
		std::map<int, uint32_t> Slots;
		std::vector<uint32_t> Free;
		std::vector<uint32_t> Dirty;
		uint32_t Generation = 0;
		uint32_t Capacity = 0;
		std::shared_ptr<UniformBufferInterface> View;
		std::shared_ptr<StorageBufferInterface> Input;
		std::shared_ptr<StorageBufferInterface> Selection;

		bool available() {
			return Select.resolve(
				LodShader, { { { "LodInput", Type::STORAGE_BUFFER }, { "LodSelection", Type::STORAGE_BUFFER }, { "View", Type::UNIFORM_BUFFER } } });
		}

		uint32_t update(const GpuSceneLod& lod, uint64_t frame) {
			auto slot = Slots.find(lod.Owner);
			if (slot == Slots.end()) {
				if (Rows.size() == MaxInstancesPerBin && Free.empty()) {
					return UINT32_MAX;
				}
				const uint32_t index = Free.empty() ? uint32_t(Rows.size()) : Free.back();
				if (Free.empty()) {
					Rows.emplace_back();
				}
				else {
					Free.pop_back();
				}
				slot = Slots.emplace(lod.Owner, index).first;
			}
			auto& row = Rows[slot->second];
			LodGpu data { { lod.Position.x, lod.Position.y, lod.Position.z, 0 }, lod.Distances, { lod.Levels, row.Data.State[1], 0, 0 } };
			if (row.Data.State[0] != lod.Levels || row.Data.Distances != lod.Distances) {
				data.State[1] = ++Generation;
			}
			row.LastSeen = frame;
			if (std::memcmp(&row.Data, &data, sizeof(data)) != 0) {
				row.Data = data;
				Dirty.push_back(slot->second);
			}
			return slot->second;
		}

		void record(Renderer& renderer, uint64_t frame, const MATH::Vector3f& camera) {
			for (auto slot = Slots.begin(); slot != Slots.end();) {
				auto& row = Rows[slot->second];
				if (row.LastSeen == frame) {
					++slot;
					continue;
				}
				row.Data.State[0] = 0;
				Dirty.push_back(slot->second);
				Free.push_back(slot->second);
				slot = Slots.erase(slot);
			}
			const bool resized = Rows.size() > Capacity;
			if (resized || !Selection) {
				Capacity = std::bit_ceil(std::max(uint32_t(Rows.size()), 1u));
				Selection = renderer.createStorageBuffer(nullptr, Capacity, 2 * sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST);
				renderer.fillBuffer(Selection, 0, Capacity * 2 * sizeof(uint32_t), 0);
			}
			if (Slots.empty()) {
				return;
			}
			if (resized || !Input) {
				Input = renderer.createStorageBuffer(nullptr, Capacity, sizeof(LodGpu), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST);
			}
			if (!View) {
				View = renderer.createUniformBuffer(nullptr, sizeof(LodView));
			}
			UploadRows(renderer, Input, Rows, Dirty, resized);
			const LodView view { { camera.x, camera.y, camera.z, 0 }, { uint32_t(Rows.size()), 0, 0, 0 } };
			renderer.updateUniformBuffer(View, view);
			renderer.setShader(Select.Shader);
			renderer.setStorageBuffer(Select.Bindings[0], Input);
			renderer.setStorageBuffer(Select.Bindings[1], Selection);
			renderer.setUniformBuffer(Select.Bindings[2], View);
			renderer.dispatch((view.Counts[0] + WorkGroupSize - 1) / WorkGroupSize, 1, 1);
		}
	};
}

struct GpuScene::State {
	ComputeShader<7> Cull;
	bool CapabilityChecked = false;
	bool Supported = false;
	std::shared_ptr<UniformBufferInterface> ViewBuffer;
	uint64_t Frame = 0;
	std::map<BinKey, Bin> Bins;
	LodTable Lods;
	std::vector<Draw> Draws;
	std::vector<Draw> ShadowDraws;
	CullView View;
	MATH::Matrix4f CurrentProjection = MATH::Matrix4f::Identity;
	std::shared_ptr<TextureInterface> ExpectedHzb;
	std::shared_ptr<TextureInterface> PreviousHzb;
	std::shared_ptr<TextureInterface> Fallback;
	bool TestHistory = false;

	void record(Renderer& renderer, const Bin& bin, const GpuSceneDraw& output, const std::shared_ptr<TextureInterface>& hzb, CullPhase phase,
		CullView& view);
};

void GpuScene::State::record(
	Renderer& renderer, const Bin& bin, const GpuSceneDraw& output, const std::shared_ptr<TextureInterface>& hzb, CullPhase phase, CullView& view) {
	const DrawIndexedIndirectCommand args { uint32_t(bin.Mesh->getIndexCount()), 0, 0, 0, 0 };
	renderer.updateStorageBufferRange(output.Arguments, &args, sizeof(args), 0);
	renderer.setShader(Cull.Shader);
	renderer.setStorageBuffer(Cull.Bindings[0], bin.Input);
	renderer.setStorageBuffer(Cull.Bindings[1], output.Transforms);
	renderer.setStorageBuffer(Cull.Bindings[2], output.Arguments);
	view.Counts[0] = uint32_t(bin.Rows.size());
	view.Counts[1] = static_cast<uint32_t>(phase);
	if (!ViewBuffer) {
		ViewBuffer = renderer.createUniformBuffer(nullptr, sizeof(view));
	}
	renderer.updateUniformBuffer(ViewBuffer, view);
	renderer.setUniformBuffer(Cull.Bindings[3], ViewBuffer);
	renderer.setStorageBuffer(Cull.Bindings[4], Lods.Selection);
	renderer.setTexture(Cull.Bindings[5], hzb);
	renderer.setStorageBuffer(Cull.Bindings[6], bin.Rejected);
	renderer.dispatch((view.Counts[0] + WorkGroupSize - 1) / WorkGroupSize, 1, 1);
}

GpuScene::GpuScene()
	: mState(std::make_unique<State>()) { }
GpuScene::~GpuScene() = default;

bool GpuScene::available(Renderer& renderer) {
	if (!mState->CapabilityChecked) {
		mState->CapabilityChecked = true;
		const auto features = renderer.gpuFeatures();
		mState->Supported = features.Compute && features.BufferUsageFlags && features.BufferTransfer && features.DrawIndirect
			&& features.MaxComputeWorkGroupCountX >= MaxInstancesPerBin / WorkGroupSize;
		if (!mState->Supported) {
			std::cout << "GPU_SCENE_FALLBACK compute, buffer upload or indexed indirect unsupported\n";
		}
	}
	return mState->Supported
		&& mState->Cull.resolve(CullShader,
			{ { { "Candidates", Type::STORAGE_BUFFER }, { "Visible", Type::STORAGE_BUFFER }, { "Arguments", Type::STORAGE_BUFFER },
			{ "Cull", Type::UNIFORM_BUFFER }, { "LodSelection", Type::STORAGE_BUFFER }, { "Hzb", Type::SAMPLER_2D },
			{ "Occlusion", Type::STORAGE_BUFFER } } });
}

void GpuScene::prepare(Renderer& renderer, const GpuSceneSnapshot& snapshot, const Frustum* frustum, const MATH::Vector3f& cameraPosition,
	const GpuSceneOcclusion& occlusion) {
	auto& state = *mState;
	++state.Frame;
	state.Draws.clear();
	const auto add = [&](const GpuSceneInstance& instance, uint32_t lodIndex) {
		auto& bin = state.Bins[{ instance.Mesh.get(), instance.Material.get() }];
		bin.Mesh = instance.Mesh;
		bin.Material = instance.Material;
		const SlotKey key { instance.Owner, instance.Lod };
		auto slot = bin.Slots.find(key);
		if (slot == bin.Slots.end()) {
			if (bin.Rows.size() == MaxInstancesPerBin && bin.Free.empty()) {
				return;
			}
			uint32_t index = uint32_t(bin.Rows.size());
			if (bin.Free.empty()) {
				bin.Rows.emplace_back();
			}
			else {
				index = bin.Free.back();
				bin.Free.pop_back();
			}
			slot = bin.Slots.emplace(key, index).first;
		}
		auto& row = bin.Rows[slot->second];
		row.LastSeen = state.Frame;
		const auto& transform = instance.Transform;
		const InstanceGpu data { { MATH::Matrix4f::Transpose(transform.World), MATH::Matrix4f::Transpose(transform.PreviousWorld), transform.Tint },
			PackBounds(instance.ModelBounds), PackBounds(instance.MeshBounds), { lodIndex, instance.Lod, 0, 0 },
			PackBounds(instance.Mesh->getBoundingSphere()) };
		if (std::memcmp(&row.Data, &data, sizeof(data)) != 0) {
			row.Data = data;
			bin.Dirty.push_back(slot->second);
		}
	};
	for (const auto& instance : snapshot.Instances) {
		add(instance, UINT32_MAX);
	}
	if (!snapshot.Lods.empty() && state.Lods.available()) {
		for (const auto& lod : snapshot.Lods) {
			// Reserve the whole object across its bins before suppressing its CPU draw.
			std::map<BinKey, std::set<uint32_t>> missing;
			for (const auto& instance : lod.Instances) {
				const BinKey key { instance.Mesh.get(), instance.Material.get() };
				const auto bin = state.Bins.find(key);
				if (bin == state.Bins.end() || !bin->second.Slots.contains({ lod.Owner, instance.Lod })) {
					missing[key].insert(instance.Lod);
				}
			}
			const bool fits = std::ranges::all_of(missing, [&](const auto& entry) {
				const auto bin = state.Bins.find(entry.first);
				return bin == state.Bins.end() || entry.second.size() <= MaxInstancesPerBin - bin->second.Slots.size();
			});
			if (!fits) {
				continue;
			}
			const uint32_t lodIndex = state.Lods.update(lod, state.Frame);
			if (lodIndex == UINT32_MAX) {
				continue;
			}
			for (const auto& instance : lod.Instances) {
				add(instance, lodIndex);
			}
		}
	}
	state.Lods.record(renderer, state.Frame, cameraPosition);

	auto& view = state.View;
	view = {};
	state.ExpectedHzb = occlusion.Hzb;
	state.Fallback = occlusion.Fallback;
	state.TestHistory = occlusion.HistoryValid && state.ExpectedHzb && state.ExpectedHzb == state.PreviousHzb;
	state.PreviousHzb.reset();
	state.CurrentProjection = occlusion.ViewProjection;
	if (state.TestHistory) {
		SetCullProjection(view, occlusion.PreviousViewProjection);
	}
	SetCullPlanes(view, frustum);
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
		const bool resized = bin.Rows.size() > bin.Capacity;
		if (resized) {
			bin.Capacity = std::bit_ceil(std::max(uint32_t(bin.Rows.size()), 16u));
			bin.Input = renderer.createStorageBuffer(nullptr, bin.Capacity, sizeof(InstanceGpu), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST);
		}
		UploadRows(renderer, bin.Input, bin.Rows, bin.Dirty, resized);
		EnsureDrawBuffers(renderer, bin.Draw, bin.Capacity);
		const auto rejectedSize = state.ExpectedHzb ? bin.Capacity : 1u;
		if (!bin.Rejected || bin.Rejected->getSizeByte() < rejectedSize * sizeof(uint32_t)) {
			bin.Rejected = renderer.createStorageBuffer(nullptr, rejectedSize, sizeof(uint32_t), BufferUsage::STORAGE);
		}
		state.record(renderer, bin, bin.Draw, state.TestHistory ? state.ExpectedHzb : state.Fallback,
			state.TestHistory ? CullPhase::PreviousHzb : CullPhase::Frustum, view);
		bin.CastsShadow
			= bin.Material->isCastShadow() && std::ranges::any_of(bin.Slots, [](const auto& slot) { return slot.first.second == UINT32_MAX; });
		state.Draws.push_back({ bin.Mesh, bin.Material, bin.Draw });
	}
}

bool GpuScene::recordOcclusionPost(Renderer& renderer, const std::shared_ptr<TextureInterface>& hzb) {
	auto& state = *mState;
	if (!state.ExpectedHzb) {
		return false;
	}
	const bool valid = hzb == state.ExpectedHzb;
	state.PreviousHzb = valid ? hzb : nullptr;
	if (!state.TestHistory || state.Bins.empty()) {
		return false;
	}
	SetCullProjection(state.View, state.CurrentProjection);
	for (const auto& [key, bin] : state.Bins) {
		state.record(renderer, bin, bin.Draw, valid ? hzb : state.Fallback, valid ? CullPhase::CurrentHzb : CullPhase::Recover, state.View);
	}
	return true;
}

bool GpuScene::recordShadow(Renderer& renderer, const Frustum& frustum) {
	auto& state = *mState;
	state.ShadowDraws.clear();
	CullView view;
	SetCullPlanes(view, &frustum);
	for (auto& [key, bin] : state.Bins) {
		if (!bin.CastsShadow) {
			continue;
		}
		EnsureDrawBuffers(renderer, bin.ShadowDraw, bin.Capacity);
		state.record(renderer, bin, bin.ShadowDraw, state.Fallback, CullPhase::Shadow, view);
		state.ShadowDraws.push_back({ bin.Mesh, bin.Material, bin.ShadowDraw });
	}
	return !state.ShadowDraws.empty();
}

bool GpuScene::contains(const Drawable& drawable) const {
	const auto bin = mState->Bins.find({ drawable.mesh.get(), drawable.material.get() });
	if (bin == mState->Bins.end()) {
		return false;
	}
	const auto slot = bin->second.Slots.lower_bound({ drawable.OwnerId, 0 });
	return slot != bin->second.Slots.end() && slot->first.first == drawable.OwnerId;
}

bool GpuScene::containsShadow(const Drawable& drawable) const {
	const auto bin = mState->Bins.find({ drawable.mesh.get(), drawable.material.get() });
	return bin != mState->Bins.end() && bin->second.CastsShadow && bin->second.Slots.contains({ drawable.OwnerId, UINT32_MAX });
}

std::span<const GpuScene::Draw> GpuScene::draws() const {
	return mState->Draws;
}
std::span<const GpuScene::Draw> GpuScene::shadowDraws() const {
	return mState->ShadowDraws;
}
}
#endif
