#include "virtualGeometryRenderer.h"

#include "coreModule/ecs/object.h"
#include "ecsModule/world.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/vertexBufferInterface.h"
#include "renderModule/diagnostics/renderStatistics.h"
#include "renderModule/drawable.h"
#include "renderModule/render.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/scene.h"
#include "virtualMeshAsset.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numeric>

namespace IKIGAI::VG {
namespace {
	constexpr const char* ClusterMaterialPath = "/shaders/virtualGeometry/vgGBuffer.mat";
	constexpr const char* InstanceBufferName = "engine_VgInstances";
	constexpr const char* DrawBufferName = "engine_VgDraws";
	constexpr uint32_t ClusterVertexBinding = 0;
	constexpr uint32_t DrawIndexBinding = 1;
	constexpr uint32_t MinimumCapacity = 256;
	// Upload granularity: keeps DX12 upload-ring allocations inside its 4 MiB pages.
	constexpr size_t UploadChunkBytes = 2u * 1024u * 1024u;

	uint32_t GrowCapacity(uint32_t current, size_t required) {
		uint64_t capacity = std::max<uint64_t>(current, MinimumCapacity);
		while (capacity < required) {
			capacity *= 2;
		}
		if (capacity > UINT32_MAX) {
			throw std::runtime_error("Virtual geometry draw count exceeds 32-bit range");
		}
		return static_cast<uint32_t>(capacity);
	}

	void UploadChunked(RENDER::StorageBufferInterface& buffer, const void* data, size_t bytes) {
		const auto* source = static_cast<const uint8_t*>(data);
		for (size_t offset = 0; offset < bytes; offset += UploadChunkBytes) {
			buffer.setSubData(source + offset, std::min(UploadChunkBytes, bytes - offset), offset);
		}
	}

	void InsertDrawable(RENDER::RenderChunk& chunk, float distance, const RENDER::Drawable& drawable) {
		const auto& material = drawable.material;
		if (material->isBlendable()) {
			if (material->isDeferred()) {
				chunk.transparentDrawablesDeferred.emplace(distance, drawable);
			}
			else {
				chunk.transparentDrawablesForward.emplace(distance, drawable);
			}
		}
		else if (material->isDeferred()) {
			chunk.opaqueDrawablesDeferred.emplace(distance, drawable);
		}
		else {
			chunk.opaqueDrawablesForward.emplace(distance, drawable);
		}
	}

	MATH::Matrix4f ShaderMatrix(const MATH::Matrix4f& matrix) {
		// The math library is row-major; shaders read column-major mat4 (same convention as the model push constant).
		return MATH::Matrix4f::Transpose(matrix);
	}
}

VirtualGeometryRenderer::VirtualGeometryRenderer() {
	mLayout.streams.push_back({ ClusterVertexBinding, static_cast<uint32_t>(sizeof(VMeshVertex)), RENDER::VertexInputRate::PerVertex, 1 });
	mLayout.streams.push_back({ DrawIndexBinding, static_cast<uint32_t>(sizeof(uint32_t)), RENDER::VertexInputRate::PerInstance, 1 });
	mLayout.attributes.push_back({ 0, ClusterVertexBinding, RENDER::VertexFormat::Float3, static_cast<uint32_t>(offsetof(VMeshVertex, Position)) });
	mLayout.attributes.push_back({ 1, ClusterVertexBinding, RENDER::VertexFormat::UInt, static_cast<uint32_t>(offsetof(VMeshVertex, Normal)) });
	mLayout.attributes.push_back({ 2, ClusterVertexBinding, RENDER::VertexFormat::UInt, static_cast<uint32_t>(offsetof(VMeshVertex, Tangent)) });
	mLayout.attributes.push_back({ 3, ClusterVertexBinding, RENDER::VertexFormat::Float2, static_cast<uint32_t>(offsetof(VMeshVertex, Uv)) });
	mLayout.attributes.push_back({ 4, DrawIndexBinding, RENDER::VertexFormat::UInt, 0 });
	mLayout.validate();
}

VirtualGeometryRenderer::~VirtualGeometryRenderer() = default;

void VirtualGeometryRenderer::setSettings(const VirtualGeometrySettings& settings) {
	if (!std::isfinite(settings.ThresholdScale) || settings.ThresholdScale < 0.0f) {
		throw std::invalid_argument("VirtualGeometrySettings.ThresholdScale must be finite and non-negative");
	}
	if (!settings.FreezeLod) {
		mFrozenCuts.clear();
	}
	mSettings = settings;
}

const VirtualGeometrySettings& VirtualGeometryRenderer::settings() const {
	return mSettings;
}

const VirtualGeometryFrameStatistics& VirtualGeometryRenderer::lastFrame() const {
	return mStatistics;
}

bool VirtualGeometryRenderer::clusterPathAvailable() const {
	return mClusterPathAvailable;
}

void VirtualGeometryRenderer::endFrame() {
	mBatches.clear();
	mInstances.clear();
	mDrawCount = 0;
}

bool VirtualGeometryRenderer::ensureClusterMaterial() {
	if (mMaterialLoadAttempted) {
		return mClusterPathAvailable;
	}
	mMaterialLoadAttempted = true;
	try {
		mClusterMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(ClusterMaterialPath);
	} catch (const std::exception& error) {
		std::cerr << "VG_CLUSTER_MATERIAL_FAIL " << error.what() << std::endl;
	}
	if (!mClusterMaterial || !mClusterMaterial->hasShader()) {
		std::cerr << "VG_CLUSTER_PATH_DISABLED reason=material path=" << ClusterMaterialPath << std::endl;
		mClusterMaterial.reset();
		mClusterPathAvailable = false;
		return false;
	}
	for (const auto& uniform : mClusterMaterial->getShader()->getReflection().mUniforms) {
		if (mClusterMaterial->isEngineUniform(uniform.mName)) {
			continue;
		}
		const auto capture = [&](const std::string& key) {
			if (const auto* value = mClusterMaterial->findUniform(key)) {
				mClusterMaterialDefaults[key] = *value;
			}
		};
		if (uniform.mMembers.empty()) {
			capture(uniform.mName);
		}
		for (const auto& member : uniform.mMembers) {
			capture(uniform.mName + member.mName);
		}
	}
	return true;
}

void VirtualGeometryRenderer::copySurfaceUniforms(const RENDER::MaterialInterface& surface) {
	// Same parameter mapping as the G-buffer override path in GameRenderer::drawDrawable.
	for (const auto& [key, value] : mClusterMaterialDefaults) {
		mClusterMaterial->set(key, value);
	}
	const auto copyUniform = [&](const std::string& target, const std::string& source) {
		const auto* value = surface.findUniform(source);
		if (!value || !mClusterMaterial->findUniform(target)) {
			return false;
		}
		mClusterMaterial->set(target, *value);
		return true;
	};
	for (const auto& uniform : mClusterMaterial->getShader()->getReflection().mUniforms) {
		if (mClusterMaterial->isEngineUniform(uniform.mName)) {
			continue;
		}
		if (uniform.mMembers.empty()) {
			if (!copyUniform(uniform.mName, uniform.mName) && uniform.mName == "u_AlbedoMap") {
				copyUniform(uniform.mName, "u_DiffuseMap");
			}
			continue;
		}
		for (const auto& member : uniform.mMembers) {
			const auto key = uniform.mName + member.mName;
			if (!copyUniform(key, key) && !copyUniform(key, member.mName) && member.mName == "u_Albedo") {
				copyUniform(key, "data_UBOu_Diffuse");
			}
		}
	}
}

void VirtualGeometryRenderer::appendFallback(ECS::VirtualMeshRenderer& component, const MATH::Matrix4f& world, const MATH::Matrix4f& previousWorld,
	int ownerId, float distance, std::vector<RENDER::RenderChunk>& chunks) const {
	const auto model = component.getFallbackModel();
	if (!model || chunks.empty()) {
		return;
	}
	for (const auto& mesh : model->getMeshes()) {
		auto material = component.getMaterial(static_cast<uint32_t>(mesh->getMaterialIndex()));
		if (!material || (!material->getShader() && !material->isDeferred())) {
			continue;
		}
		RENDER::Drawable drawable;
		drawable.prevWorld = previousWorld;
		drawable.world = world;
		drawable.mesh = mesh;
		drawable.material = std::move(material);
		drawable.OwnerId = ownerId;
		InsertDrawable(chunks.front(), distance, drawable);
	}
}

void VirtualGeometryRenderer::prepareFrame(RENDER::Renderer& renderer, SCENE_SYSTEM::Scene& scene, const VirtualGeometryFrameInput& input,
	std::vector<RENDER::RenderChunk>& viewChunks, std::vector<RENDER::RenderChunk>& shadowChunks) {
	const auto start = std::chrono::steady_clock::now();
	endFrame();
	mStatistics = {};
	if (!mSettings.FreezeLod) {
		mFrozenCuts.clear();
	}
	auto& components = scene.getWorld().getComponentManager()->getComponentsArray<ECS::VirtualMeshRenderer>();
	const bool clusterPath = mSettings.Enabled && input.GeometryStageActive && IsVirtualGeometryGpuPathSupported() && ensureClusterMaterial();
	mMaxDrawIndirectCount = clusterPath ? std::max(1u, renderer.gpuFeatures().MaxDrawIndirectCount) : 1u;
	VirtualGeometrySelectionSettings selection;
	selection.FrustumCulling = mSettings.FrustumCulling;
	selection.ConeCulling = mSettings.ConeCulling;
	VirtualGeometrySelectionStatistics selectionStatistics;
	for (auto& component : components) {
		auto& object = component.obj;
		if (!object->getIsVisible() || !object->getIsActive()) {
			continue;
		}
		++mStatistics.Instances;
		auto& transform = object->getTransform()->getTransform();
		const auto world = transform.getWorldMatrix();
		const auto previousWorld = component.advanceHistory(world, input.Frame);
		const int ownerId = static_cast<int>(object->getID().getUniqueId());
		const float distance = MATH::Vector3f::Distance(transform.getWorldPosition(), input.View.CameraPosition);
		// Shadow maps, ray tracing and GI see the fallback mesh in M2.
		appendFallback(component, world, previousWorld, ownerId, distance, shadowChunks);

		const auto& asset = component.getAsset();
		bool drawClusters = clusterPath && asset && asset->hasClusterBuffers();
		bool singleSided = true;
		for (uint32_t slot = 0; drawClusters && slot < asset->materialSlotCount(); ++slot) {
			const auto material = component.getMaterial(slot);
			if (!material || material->isBlendable()) {
				drawClusters = false;
				break;
			}
			singleSided = singleSided && material->isBackfaceCulling();
		}
		if (!drawClusters) {
			++mStatistics.FallbackInstances;
			appendFallback(component, world, previousWorld, ownerId, distance, viewChunks);
			continue;
		}
		++mStatistics.ClusterInstances;
		const auto instance = MakeInstanceTransform(world, singleSided);
		if (mSettings.FrustumCulling && !IsInstanceVisible(asset->header().BoundingSphere, instance, input.View)) {
			++mStatistics.InstancesCulled;
			continue;
		}
		const float threshold = component.getErrorThreshold() * mSettings.ThresholdScale;
		mScratchClusters.clear();
		if (mSettings.FreezeLod) {
			auto& frozen = mFrozenCuts[ownerId];
			if (frozen.Asset != asset.get()) {
				frozen.Asset = asset.get();
				frozen.Clusters.clear();
				SelectLodCut(asset->clusters(), instance, input.View, threshold, frozen.Clusters);
			}
			mScratchClusters = frozen.Clusters;
		}
		else {
			SelectLodCut(asset->clusters(), instance, input.View, threshold, mScratchClusters);
		}
		mStatistics.LodClusters += static_cast<uint32_t>(mScratchClusters.size());
		for (const uint32_t clusterIndex : mScratchClusters) {
			mStatistics.LodTriangles += asset->clusters()[clusterIndex].IndexCount / 3;
		}
		CullClusters(asset->clusters(), instance, input.View, selection, mScratchClusters, selectionStatistics);
		if (mScratchClusters.empty()) {
			continue;
		}

		const auto instanceIndex = static_cast<uint32_t>(mInstances.size());
		InstanceGpu gpu;
		gpu.World = ShaderMatrix(world);
		gpu.PreviousWorld = ShaderMatrix(previousWorld);
		// Shader reads mat3(Normal) = transpose(inverse(mat3(World))); the uploaded matrix is the inverse itself.
		gpu.Normal = instance.MaxScale > 0.0f ? MATH::Matrix4f::Inverse(world) : MATH::Matrix4f::Identity;
		const auto debugMode = mSettings.DebugOverride != ECS::VirtualMeshDebugMode::NONE ? mSettings.DebugOverride : component.getDebugMode();
		gpu.Params = { static_cast<float>(debugMode), 0.0f, 0.0f, 0.0f };
		mInstances.push_back(gpu);

		// Resolve the batch of every material slot once per instance (map lookups stay out of the cluster loop).
		const auto& clusters = asset->clusters();
		mSlotBatches.assign(asset->materialSlotCount(), nullptr);
		for (uint32_t slot = 0; slot < asset->materialSlotCount(); ++slot) {
			auto material = component.getMaterial(slot);
			const BatchKey key { asset.get(), slot, material.get() };
			auto [found, inserted] = mBatches.try_emplace(key);
			if (inserted) {
				found->second.Asset = asset;
				found->second.Material = std::move(material);
				found->second.Slot = slot;
			}
			mSlotBatches[slot] = &found->second;
		}
		for (const uint32_t clusterIndex : mScratchClusters) {
			const auto& cluster = clusters[clusterIndex];
			auto& batch = *mSlotBatches[cluster.MaterialSlot];
			RENDER::DrawIndexedIndirectCommand command;
			command.IndexCount = cluster.IndexCount;
			command.InstanceCount = 1;
			command.FirstIndex = cluster.IndexOffset;
			command.VertexOffset = 0;
			command.FirstInstance = 0; // assigned in uploadFrame once batch offsets are known
			batch.Commands.push_back(command);
			batch.Records.push_back({ instanceIndex, clusterIndex, cluster.LodLevel, cluster.GroupId });
			++mStatistics.VisibleClusters;
			mStatistics.VisibleTriangles += cluster.IndexCount / 3;
			++mStatistics.LodHistogram[std::min<uint32_t>(cluster.LodLevel, 7)];
		}
	}
	for (auto it = mBatches.begin(); it != mBatches.end();) {
		it = it->second.Commands.empty() ? mBatches.erase(it) : std::next(it);
	}
	mStatistics.FrustumCulledClusters = selectionStatistics.FrustumCulled;
	mStatistics.ConeCulledClusters = selectionStatistics.ConeCulled;
	mStatistics.Batches = static_cast<uint32_t>(mBatches.size());
	if (clusterPath && !mBatches.empty()) {
		uploadFrame(renderer);
	}
	mStatistics.SelectionMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	RENDER::RenderStatistics::Get().recordVirtualGeometry(
		mStatistics.ClusterInstances, mStatistics.FallbackInstances, mStatistics.VisibleClusters, mStatistics.VisibleTriangles);
}

void VirtualGeometryRenderer::uploadFrame(RENDER::Renderer& renderer) {
	auto& commands = mCommandScratch;
	auto& records = mRecordScratch;
	commands.clear();
	records.clear();
	for (auto& [key, batch] : mBatches) {
		batch.FirstDraw = static_cast<uint32_t>(commands.size());
		batch.DrawCount = static_cast<uint32_t>(batch.Commands.size());
		for (size_t i = 0; i < batch.Commands.size(); ++i) {
			auto command = batch.Commands[i];
			// The per-draw index reaches the shader through the instance-rate stream: instance-rate attribute fetch
			// includes FirstInstance on Vulkan and DirectX 12 (SV_InstanceID would not on DX12).
			command.FirstInstance = static_cast<uint32_t>(commands.size());
			commands.push_back(command);
			records.push_back(batch.Records[i]);
		}
		batch.Commands = {};
		batch.Records = {};
	}
	mDrawCount = static_cast<uint32_t>(commands.size());

	if (!mInstanceBuffer || mInstances.size() > mInstanceCapacity) {
		mInstanceCapacity = GrowCapacity(mInstanceCapacity, mInstances.size());
		mInstanceBuffer = renderer.createStorageBuffer(nullptr, mInstanceCapacity, sizeof(InstanceGpu), RENDER::BufferUsage::STORAGE);
	}
	if (!mIndirectBuffer || mDrawCount > mDrawCapacity) {
		mDrawCapacity = GrowCapacity(mDrawCapacity, mDrawCount);
		mIndirectBuffer = renderer.createStorageBuffer(
			nullptr, mDrawCapacity, sizeof(RENDER::DrawIndexedIndirectCommand), RENDER::BufferUsage::STORAGE | RENDER::BufferUsage::INDIRECT);
		mDrawRecordBuffer = renderer.createStorageBuffer(nullptr, mDrawCapacity, sizeof(DrawRecordGpu), RENDER::BufferUsage::STORAGE);
	}
	if (!mDrawIndexStream || mDrawCount > mDrawIndexCapacity) {
		mDrawIndexCapacity = GrowCapacity(mDrawIndexCapacity, mDrawCount);
		std::vector<uint32_t> identity(mDrawIndexCapacity);
		std::iota(identity.begin(), identity.end(), 0u);
		mDrawIndexStream = CreateVirtualGeometryVertexBuffer(identity.data(), identity.size(), sizeof(uint32_t));
	}
	UploadChunked(*mInstanceBuffer, mInstances.data(), mInstances.size() * sizeof(InstanceGpu));
	UploadChunked(*mIndirectBuffer, commands.data(), commands.size() * sizeof(RENDER::DrawIndexedIndirectCommand));
	UploadChunked(*mDrawRecordBuffer, records.data(), records.size() * sizeof(DrawRecordGpu));
}

void VirtualGeometryRenderer::drawGeometry(RENDER::Renderer& renderer, const VirtualGeometryDrawBindings& bindings) {
	if (mBatches.empty() || !mClusterMaterial || !mDrawCount) {
		return;
	}
	auto* driver = renderer.getDriver();
	const auto shader = mClusterMaterial->getShader();
	const auto& reflected = shader->getReflection().mNameToUniforms;
	constexpr uint32_t stride = sizeof(RENDER::DrawIndexedIndirectCommand);
	for (auto& [key, batch] : mBatches) {
		const auto drawCount = batch.DrawCount;
		if (!drawCount) {
			continue;
		}
		copySurfaceUniforms(*batch.Material);
		renderer.setDepth({ mClusterMaterial->isDepthTest() ? mClusterMaterial->getDepthFunc() : RENDER::DepthFunction::ALWAYS,
			mClusterMaterial->isDepthWriting() });
		renderer.setCull(batch.Material->isBackfaceCulling() ? RENDER::CullFace::BACK : RENDER::CullFace::NONE);
		renderer.resetBlending();
		renderer.setShader(shader);
		mClusterMaterial->bind(bindings.EmptyTexture, true);
		if (reflected.contains("engine_UBO")) {
			renderer.setUniformBuffer("engine_UBO", bindings.EngineUbo);
		}
		if (reflected.contains("engine_Frame")) {
			renderer.setUniformBuffer("engine_Frame", bindings.FrameUbo);
		}
		if (reflected.contains(InstanceBufferName)) {
			renderer.setStorageBuffer(InstanceBufferName, mInstanceBuffer);
		}
		if (reflected.contains(DrawBufferName)) {
			renderer.setStorageBuffer(DrawBufferName, mDrawRecordBuffer);
		}
		const auto& gpu = batch.Asset->gpuBuffers();
		renderer.execute([driver, layout = mLayout, vertices = gpu.Vertices, indices = gpu.Indices, drawIndex = mDrawIndexStream]() {
			driver->setVertexStreams(layout, { { ClusterVertexBinding, vertices }, { DrawIndexBinding, drawIndex } });
			driver->setIndexBuffer(indices);
		});
		renderer.setPrimitiveMode(RENDER::PrimitiveMode::TRIANGLES);
		for (uint32_t done = 0; done < drawCount;) {
			const uint32_t count = std::min(drawCount - done, mMaxDrawIndirectCount);
			renderer.drawIndexedIndirect(mIndirectBuffer, size_t(batch.FirstDraw + done) * stride, count, stride);
			++mStatistics.IndirectCalls;
			done += count;
		}
		mClusterMaterial->unbind();
	}
	renderer.execute([driver]() { driver->resetVertexStreams(); });
}
}
