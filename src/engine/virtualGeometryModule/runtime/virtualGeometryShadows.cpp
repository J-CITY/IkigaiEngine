#include "virtualGeometryShadows.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/virtualMeshRenderer.h"
#include "ecsModule/world.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "renderModule/drawable.h"
#include "renderModule/render.h"
#include "sceneModule/scene.h"
#include "virtualMeshAsset.h"

namespace IKIGAI::VG {
namespace {
	bool HasOpaqueAlpha(const RENDER::MaterialInterface& material) {
		for (const auto* name : { "u_AlbedoMap", "u_DiffuseMap" }) {
			if (const auto* uniform = material.findUniform(name)) {
				const auto* texture = std::get_if<std::shared_ptr<RENDER::TextureInterface>>(uniform);
				// Texture alpha can carry coverage even when Blendable is false.
				if (!texture || *texture) {
					return false;
				}
			}
		}
		for (const auto* name : { "data_UBOu_Albedo", "u_Albedo", "data_UBOu_Diffuse" }) {
			if (const auto* uniform = material.findUniform(name)) {
				const auto* color = std::get_if<MATH::Vector4f>(uniform);
				if (!color || !(color->w >= 0.5f)) {
					return false;
				}
			}
		}
		return true;
	}

	bool IsStaticOpaque(const RENDER::MaterialInterface& material) {
		if (material.getGPUInstances() != 1 || material.isBlendable() || material.deformation().Mode != RENDER::DeformationMode::None
			|| !material.pbrLayers().Layers.empty() || !HasOpaqueAlpha(material)) {
			return false;
		}
		const auto shader = material.getShader();
		return shader
			&& (shader->mPath == "/shaders/render/portable/gbuffer.shader" || shader->mPath == "/shaders/render/portable/gbufferEmissive.shader"
				|| shader->mPath == "/shaders/virtualGeometry/vgGBuffer.shader");
	}
}

void ValidateVirtualGeometryShadowSettings(const VirtualGeometryShadowSettings& settings) {
	if (!settings.MaxClusterTests || !settings.MaxDraws || settings.MaxDraws > 65536 || !std::isfinite(settings.ThresholdScale)
		|| settings.ThresholdScale < 0.0f) {
		throw std::invalid_argument("Invalid VirtualGeometryShadows cascade budget or threshold scale");
	}
}

void VirtualGeometryShadows::prepare(SCENE_SYSTEM::Scene& scene, bool enabled) {
	endFrame();
	mStatistics = {};
	if (!enabled) {
		return;
	}
	auto& components = scene.getWorld().getComponentManager()->getComponentsArray<ECS::VirtualMeshRenderer>();
	for (const auto& component : components) {
		const auto& object = component.obj.get();
		const auto& asset = component.getAsset();
		if (!object.getIsVisible() || !object.getIsActive() || object.getComponent<ECS::ModelRenderer>() || !asset || !asset->hasClusterBuffers()
			|| asset->streamsPages()) {
			continue;
		}
		const auto fallback = component.getFallbackModel();
		if (!fallback) {
			continue;
		}
		Candidate candidate;
		candidate.Asset = asset;
		candidate.Fallback = fallback;
		candidate.OwnerId = static_cast<int>(object.getID().getUniqueId());
		candidate.Threshold = component.getErrorThreshold();
		bool hasCaster = false;
		bool supported = true;
		for (uint32_t slot = 0; slot < asset->materialSlotCount(); ++slot) {
			const auto material = component.getMaterial(slot);
			const bool casts = material && material->isCastShadow();
			candidate.CastsShadow.push_back(casts);
			if (!casts) {
				continue;
			}
			const bool cullBack = material->isBackfaceCulling();
			if (!IsStaticOpaque(*material) || (hasCaster && cullBack != candidate.CullBack)) {
				supported = false;
				break;
			}
			candidate.CullBack = cullBack;
			hasCaster = true;
		}
		if (supported && hasCaster) {
			candidate.Transform = MakeInstanceTransform(object.getTransform()->getTransform().getWorldMatrix(), false);
			mCandidates.push_back(std::move(candidate));
		}
	}
}

void VirtualGeometryShadows::endFrame() {
	mCandidates.clear();
	mReplaced.clear();
}

void VirtualGeometryShadows::record(RENDER::Renderer& renderer, const VirtualGeometryView& view, const VirtualGeometryShadowSettings& settings,
	const std::shared_ptr<RENDER::ShaderInterface>& shader, const std::shared_ptr<RENDER::UniformBufferInterface>& shadowPass, RENDER::Depth depth) {
	mReplaced.clear();
	const auto features = renderer.gpuFeatures();
	if (!shader || !features.DrawIndirect || !features.MaxDrawIndirectCount) {
		return;
	}

	++mStatistics.Cascades;
	struct Batch {
		const Candidate* Caster = nullptr;
		uint32_t FirstDraw = 0;
		uint32_t Count = 0;
	};
	std::vector<Batch> batches;
	std::vector<RENDER::DrawIndexedIndirectCommand> commands;
	std::vector<uint32_t> selected;
	uint32_t remainingTests = settings.MaxClusterTests;
	const auto replace = [&](const Candidate& candidate) {
		for (const auto& mesh : candidate.Fallback->getMeshes()) {
			mReplaced.emplace(candidate.OwnerId, mesh.get());
		}
	};
	for (const auto& candidate : mCandidates) {
		if (!IsInstanceVisible(candidate.Asset->header().BoundingSphere, candidate.Transform, view)) {
			replace(candidate);
			continue;
		}
		const auto& clusters = candidate.Asset->clusters();
		const float threshold = candidate.Threshold * settings.ThresholdScale;
		if (!std::isfinite(threshold) || clusters.size() > remainingTests) {
			++mStatistics.BudgetFallbacks;
			continue;
		}
		remainingTests -= static_cast<uint32_t>(clusters.size());
		selected.clear();
		SelectLodCut(clusters, candidate.Transform, view, threshold, selected);
		if (selected.empty()) {
			continue;
		}
		VirtualGeometrySelectionStatistics stats;
		CullClusters(clusters, candidate.Transform, view, { threshold, true, false }, selected, stats);
		std::erase_if(selected, [&](uint32_t cluster) { return !candidate.CastsShadow[clusters[cluster].MaterialSlot]; });
		if (selected.size() > settings.MaxDraws - commands.size()) {
			++mStatistics.BudgetFallbacks;
			continue;
		}
		if (!selected.empty()) {
			batches.push_back({ &candidate, static_cast<uint32_t>(commands.size()), static_cast<uint32_t>(selected.size()) });
			for (const auto clusterId : selected) {
				const auto& cluster = clusters[clusterId];
				RENDER::DrawIndexedIndirectCommand command;
				command.IndexCount = cluster.IndexCount;
				command.InstanceCount = 1;
				command.FirstIndex = cluster.IndexOffset;
				commands.push_back(command);
				mStatistics.Triangles += cluster.IndexCount / 3;
			}
		}
		replace(candidate);
	}
	if (commands.empty()) {
		return;
	}
	mStatistics.ClusterDraws += static_cast<uint32_t>(commands.size());
	constexpr auto stride = sizeof(RENDER::DrawIndexedIndirectCommand);
	if (mArgumentCapacity < commands.size()) {
		mArguments = renderer.createStorageBuffer(nullptr, settings.MaxDraws, stride, RENDER::BufferUsage::STORAGE | RENDER::BufferUsage::INDIRECT);
		mArgumentCapacity = settings.MaxDraws;
	}
	auto* driver = renderer.getDriver();
	// The atlas has already been cleared. Vulkan must store and order its
	// attachments before the upload interrupts rendering and the next draw loads them.
	renderer.execute([driver] { driver->renderGraphBarrier(false); });
	renderer.updateStorageBufferRange(mArguments, commands.data(), commands.size() * stride, 0);
	renderer.setShader(shader);
	renderer.setUniformBuffer("engine_ShadowPass", shadowPass);
	renderer.setDepth(depth);
	renderer.resetBlending();
	renderer.setPrimitiveMode(RENDER::PrimitiveMode::TRIANGLES);

	const RENDER::VertexLayout layout {
		{ { 0, sizeof(VMeshVertex) } },
		{ { 0, 0, RENDER::VertexFormat::Float3, static_cast<uint32_t>(offsetof(VMeshVertex, Position)) } },
	};
	for (const auto& batch : batches) {
		const auto& caster = *batch.Caster;
		const auto& buffers = caster.Asset->gpuBuffers();
		renderer.setCull(caster.CullBack ? RENDER::CullFace::BACK : RENDER::CullFace::NONE);
		const auto model = MATH::Matrix4f::Transpose(caster.Transform.World);
		renderer.setPushConstant(RENDER::ShaderType::VERTEX, 0, sizeof(model), &model);
		renderer.execute([driver, layout, vertices = buffers.Vertices, indices = buffers.Indices] {
			driver->setVertexStreams(layout, { { 0, vertices } });
			driver->setIndexBuffer(indices);
		});
		for (uint32_t done = 0; done < batch.Count;) {
			const auto drawCount = std::min(features.MaxDrawIndirectCount, batch.Count - done);
			renderer.drawIndexedIndirect(mArguments, size_t(batch.FirstDraw + done) * stride, drawCount, stride);
			done += drawCount;
		}
	}
}

const VirtualGeometryShadowStatistics& VirtualGeometryShadows::statistics() const {
	return mStatistics;
}

bool VirtualGeometryShadows::replaces(const RENDER::Drawable& drawable) const {
	return mReplaced.contains({ drawable.OwnerId, drawable.mesh.get() });
}
}
