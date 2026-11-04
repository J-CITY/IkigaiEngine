#include "virtualGeometryRenderer.h"

#include "coreModule/ecs/object.h"
#include "ecsModule/world.h"
#include "profilerModule/profiler.h"
#include "profilerModule/profilerSpy.h"
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
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
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

	// First three rows of a row-major matrix (the GPU selection transforms points exactly like Matrix4f * Vector3f).
	void CopyRows(const MATH::Matrix4f& matrix, float rows[3][4]) {
		const auto* data = matrix.getData();
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 4; ++column) {
				rows[row][column] = data[row * 4 + column];
			}
		}
	}

	const char* SelectionPathName(VirtualGeometrySelectionPath path) {
		switch (path) {
		case VirtualGeometrySelectionPath::AUTO:
			return "auto";
		case VirtualGeometrySelectionPath::CPU:
			return "cpu";
		case VirtualGeometrySelectionPath::GPU:
			return "gpu";
		}
		return "unknown";
	}

	bool ParseSelectionPath(std::string value, VirtualGeometrySelectionPath& path) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const auto candidate : { VirtualGeometrySelectionPath::AUTO, VirtualGeometrySelectionPath::CPU, VirtualGeometrySelectionPath::GPU }) {
			if (value == SelectionPathName(candidate)) {
				path = candidate;
				return true;
			}
		}
		return false;
	}
}

void ValidateVirtualGeometrySettings(const VirtualGeometrySettings& settings) {
	if (!std::isfinite(settings.ThresholdScale) || settings.ThresholdScale < 0.0f) {
		throw std::invalid_argument("VirtualGeometrySettings.ThresholdScale must be finite and non-negative");
	}
	const auto path = static_cast<int>(settings.SelectionPath);
	if (path < static_cast<int>(VirtualGeometrySelectionPath::AUTO) || path > static_cast<int>(VirtualGeometrySelectionPath::GPU)) {
		throw std::invalid_argument("VirtualGeometrySettings.SelectionPath must be AUTO, CPU or GPU");
	}
	const auto debug = static_cast<int>(settings.DebugOverride);
	if (debug < static_cast<int>(ECS::VirtualMeshDebugMode::NONE) || debug > static_cast<int>(ECS::VirtualMeshDebugMode::GROUP)) {
		throw std::invalid_argument("VirtualGeometrySettings.DebugOverride is out of range");
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
	ValidateVirtualGeometrySettings(settings);
	if (!settings.FreezeLod) {
		mFrozenCuts.clear();
		mFrozenLodViews.clear();
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

VirtualGeometrySelectionPath VirtualGeometryRenderer::activeSelectionPath() const {
	return mActivePath;
}

const VirtualGeometryGpuFrameRecord& VirtualGeometryRenderer::lastGpuFrame() const {
	return mGpuFrameRecord;
}

VirtualGeometryGpuCapture VirtualGeometryRenderer::captureGpuSelection(RENDER::Renderer& renderer) {
	VirtualGeometryGpuCapture capture;
	if (!mGpuFrameRecorded) {
		return capture;
	}
	capture.Record = mGpuFrameRecord;
	capture.Record.View.Frustum = nullptr;
	capture.Segments = mGpuFrame.Segments;
	capture.Tickets = mGpuSelection.requestCapture(renderer, mGpuFrame);
	return capture;
}

bool VirtualGeometryRenderer::collectGpuSelection(RENDER::Renderer& renderer, const VirtualGeometryGpuCapture& capture,
	std::vector<VirtualGeometryGpuVisibleCluster>& visible, VirtualGeometryGpuStatistics& statistics) {
	return mGpuSelection.collectCapture(renderer, capture.Tickets, capture.Segments, visible, statistics);
}

void VirtualGeometryRenderer::endFrame() {
	mBatches.clear();
	mInstances.clear();
	mCandidates.clear();
	mDrawCount = 0;
	mGpuDrawActive = false;
}

VirtualGeometrySelectionPath VirtualGeometryRenderer::resolveSelectionPath(RENDER::Renderer& renderer, bool clusterPath) {
	if (!mEnvironmentPathRead) {
		mEnvironmentPathRead = true;
		if (const char* value = std::getenv("IKIGAI_VG_SELECTION")) {
			mHasEnvironmentPath = ParseSelectionPath(value, mEnvironmentPath);
			if (!mHasEnvironmentPath) {
				std::cerr << "VG_SELECTION_ENV_INVALID IKIGAI_VG_SELECTION=" << value << " (expected cpu, gpu or auto)" << std::endl;
			}
		}
	}
	const auto requested = mHasEnvironmentPath ? mEnvironmentPath : mSettings.SelectionPath;
	auto path = VirtualGeometrySelectionPath::CPU;
	std::string reason;
	if (!clusterPath) {
		reason = "cluster-path-off";
	}
	else if (requested != VirtualGeometrySelectionPath::CPU) {
		reason = mGpuSelection.unavailableReason(renderer);
		if (reason.empty()) {
			path = VirtualGeometrySelectionPath::GPU;
		}
	}
	const std::string log = std::string("path=") + SelectionPathName(path) + " requested=" + SelectionPathName(requested)
		+ (reason.empty() ? std::string() : " reason=" + reason);
	if (clusterPath && log != mLoggedSelection) {
		std::cout << "VG_SELECTION " << log << std::endl;
		mLoggedSelection = log;
	}
	return path;
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
	auto cpuProfile = PROFILER::Profiler::IsEnabled() ? std::make_unique<PROFILER::ProfilerSpy>("VirtualGeometry::prepareFrame") : nullptr;
	endFrame();
	mStatistics = {};
	if (!mSettings.FreezeLod) {
		mFrozenCuts.clear();
		mFrozenLodViews.clear();
	}
	auto& components = scene.getWorld().getComponentManager()->getComponentsArray<ECS::VirtualMeshRenderer>();
	const bool clusterPath = mSettings.Enabled && input.GeometryStageActive && IsVirtualGeometryGpuPathSupported() && ensureClusterMaterial();
	mMaxDrawIndirectCount = clusterPath ? std::max(1u, renderer.gpuFeatures().MaxDrawIndirectCount) : 1u;
	mActivePath = resolveSelectionPath(renderer, clusterPath);
	bool gpuSelection = mActivePath == VirtualGeometrySelectionPath::GPU;
	// The non-blocking readback of the previous GPU frame completed: that frame was waited for at its submit.
	VirtualGeometryGpuStatistics gpuStatistics;
	const bool previousGpuStatistics = gpuSelection && mGpuSelection.takeStatistics(renderer, gpuStatistics);
	mGpuFrameRecorded = false;

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
		// Shadow maps, ray tracing and GI see the fallback mesh (clustered shadow views are M6).
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
		InstanceCandidate candidate;
		candidate.Component = &component;
		candidate.Asset = asset;
		candidate.World = world;
		candidate.PreviousWorld = previousWorld;
		candidate.Transform = MakeInstanceTransform(world, singleSided);
		candidate.OwnerId = ownerId;
		candidate.Threshold = component.getErrorThreshold() * mSettings.ThresholdScale;
		candidate.DebugMode = mSettings.DebugOverride != ECS::VirtualMeshDebugMode::NONE ? mSettings.DebugOverride : component.getDebugMode();
		mCandidates.push_back(std::move(candidate));
	}

	if (gpuSelection && !mCandidates.empty() && !buildGpuFrame(renderer, input)) {
		// A GPU limit was exceeded this frame (logged by buildGpuFrame): select on the CPU instead.
		gpuSelection = false;
		mActivePath = VirtualGeometrySelectionPath::CPU;
		mBatches.clear();
		mInstances.clear();
	}
	if (gpuSelection) {
		mStatistics.GpuSelection = true;
		if (!mCandidates.empty()) {
			mGpuSelection.record(renderer, mGpuFrame, input.GpuProfiler);
			uploadFrame(renderer);
			mGpuDrawActive = true;
			mGpuFrameRecorded = true;
		}
		if (previousGpuStatistics) {
			applyGpuStatistics(gpuStatistics, 1);
		}
		mStatistics.Batches = static_cast<uint32_t>(mBatches.size());
		mStatistics.DrawCapacity = mGpuFrame.DrawCapacity;
		mStatistics.GpuSelectionBytes = mGpuSelection.gpuBytes();
	}
	else {
		// Drop the asset references of an earlier GPU frame (they only serve readGpuSelection).
		mGpuFrame.Assets.clear();
		mGpuFrameRecord.Instances.clear();
		VirtualGeometrySelectionSettings selection;
		selection.FrustumCulling = mSettings.FrustumCulling;
		selection.ConeCulling = mSettings.ConeCulling;
		VirtualGeometrySelectionStatistics selectionStatistics;
		for (const auto& candidate : mCandidates) {
			selectOnCpu(candidate, input, selection, selectionStatistics);
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
	}
	mStatistics.SelectionMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	RENDER::RenderStatistics::Get().recordVirtualGeometry(
		mStatistics.ClusterInstances, mStatistics.FallbackInstances, mStatistics.VisibleClusters, mStatistics.VisibleTriangles);
}

VirtualGeometryRenderer::InstanceGpu VirtualGeometryRenderer::makeInstanceGpu(const InstanceCandidate& candidate) const {
	InstanceGpu gpu;
	gpu.World = ShaderMatrix(candidate.World);
	gpu.PreviousWorld = ShaderMatrix(candidate.PreviousWorld);
	// Shader reads mat3(Normal) = transpose(inverse(mat3(World))); the uploaded matrix is the inverse itself.
	gpu.Normal = candidate.Transform.MaxScale > 0.0f ? MATH::Matrix4f::Inverse(candidate.World) : MATH::Matrix4f::Identity;
	gpu.Params = { static_cast<float>(candidate.DebugMode), 0.0f, 0.0f, 0.0f };
	return gpu;
}

VirtualGeometryRenderer::Batch& VirtualGeometryRenderer::findBatch(const InstanceCandidate& candidate, uint32_t slot) {
	auto material = candidate.Component->getMaterial(slot);
	const BatchKey key { candidate.Asset.get(), slot, material.get() };
	auto [found, inserted] = mBatches.try_emplace(key);
	if (inserted) {
		found->second.Asset = candidate.Asset;
		found->second.Material = std::move(material);
		found->second.Slot = slot;
	}
	return found->second;
}

void VirtualGeometryRenderer::selectOnCpu(const InstanceCandidate& candidate, const VirtualGeometryFrameInput& input,
	const VirtualGeometrySelectionSettings& selection, VirtualGeometrySelectionStatistics& selectionStatistics) {
	const auto& asset = candidate.Asset;
	const auto& instance = candidate.Transform;
	if (mSettings.FrustumCulling && !IsInstanceVisible(asset->header().BoundingSphere, instance, input.View)) {
		++mStatistics.InstancesCulled;
		return;
	}
	mScratchClusters.clear();
	if (mSettings.FreezeLod) {
		auto& frozen = mFrozenCuts[candidate.OwnerId];
		if (frozen.Asset != asset.get()) {
			frozen.Asset = asset.get();
			frozen.Clusters.clear();
			SelectLodCut(asset->clusters(), instance, input.View, candidate.Threshold, frozen.Clusters);
		}
		mScratchClusters = frozen.Clusters;
	}
	else {
		SelectLodCut(asset->clusters(), instance, input.View, candidate.Threshold, mScratchClusters);
	}
	mStatistics.LodClusters += static_cast<uint32_t>(mScratchClusters.size());
	for (const uint32_t clusterIndex : mScratchClusters) {
		mStatistics.LodTriangles += asset->clusters()[clusterIndex].IndexCount / 3;
	}
	CullClusters(asset->clusters(), instance, input.View, selection, mScratchClusters, selectionStatistics);
	if (mScratchClusters.empty()) {
		return;
	}

	const auto instanceIndex = static_cast<uint32_t>(mInstances.size());
	mInstances.push_back(makeInstanceGpu(candidate));

	// Resolve the batch of every material slot once per instance (map lookups stay out of the cluster loop).
	const auto& clusters = asset->clusters();
	mSlotBatches.assign(asset->materialSlotCount(), nullptr);
	for (uint32_t slot = 0; slot < asset->materialSlotCount(); ++slot) {
		mSlotBatches[slot] = &findBatch(candidate, slot);
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

bool VirtualGeometryRenderer::buildGpuFrame(RENDER::Renderer& renderer, const VirtualGeometryFrameInput& input) {
	// Instances of one asset must be contiguous: each asset has its own cluster buffer and cluster-cull dispatch.
	std::stable_sort(mCandidates.begin(), mCandidates.end(),
		[](const InstanceCandidate& left, const InstanceCandidate& right) { return left.Asset.get() < right.Asset.get(); });
	auto& frame = mGpuFrame;
	frame.Instances.clear();
	frame.SlotBatches.clear();
	frame.Segments.clear();
	frame.Assets.clear();
	frame.DrawCapacity = 0;
	auto& record = mGpuFrameRecord;
	record.Instances.clear();
	record.HasFrustum = input.View.Frustum != nullptr;
	record.Frustum = record.HasFrustum ? *input.View.Frustum : RENDER::Frustum {};
	record.View = input.View;
	record.View.Frustum = record.HasFrustum ? &record.Frustum : nullptr;
	record.Selection.FrustumCulling = mSettings.FrustumCulling;
	record.Selection.ConeCulling = mSettings.ConeCulling;

	const auto features = renderer.gpuFeatures();
	uint32_t segments = 0;
	for (const auto& candidate : mCandidates) {
		const auto& asset = candidate.Asset;
		const auto instanceIndex = static_cast<uint32_t>(frame.Instances.size());
		if (frame.Assets.empty() || frame.Assets.back().Asset != asset) {
			frame.Assets.push_back({ asset, instanceIndex, 0 });
		}
		auto& assetEntry = frame.Assets.back();
		++assetEntry.InstanceCount;
		mInstances.push_back(makeInstanceGpu(candidate));

		VirtualGeometryInstanceTransform lodTransform = candidate.Transform;
		VirtualGeometryView lodView = input.View;
		lodView.Frustum = nullptr;
		float threshold = candidate.Threshold;
		if (mSettings.FreezeLod) {
			auto& frozen = mFrozenLodViews[candidate.OwnerId];
			if (frozen.Asset != asset.get()) {
				frozen.Asset = asset.get();
				frozen.Transform = candidate.Transform;
				frozen.View = lodView;
				frozen.Threshold = candidate.Threshold;
			}
			lodTransform = frozen.Transform;
			lodView = frozen.View;
			threshold = frozen.Threshold;
		}

		VirtualGeometryCullInstanceGpu gpu;
		CopyRows(candidate.World, gpu.WorldRows);
		CopyRows(lodTransform.World, gpu.LodRows);
		const auto& bounds = asset->header().BoundingSphere;
		gpu.Bounds[0] = bounds.Center[0];
		gpu.Bounds[1] = bounds.Center[1];
		gpu.Bounds[2] = bounds.Center[2];
		gpu.Bounds[3] = bounds.Radius;
		gpu.LodCamera[0] = lodView.CameraPosition.x;
		gpu.LodCamera[1] = lodView.CameraPosition.y;
		gpu.LodCamera[2] = lodView.CameraPosition.z;
		gpu.LodCamera[3] = lodView.ProjectionScale;
		gpu.Params[0] = threshold;
		gpu.Params[1] = candidate.Transform.MaxScale;
		gpu.Params[2] = lodTransform.MaxScale;
		gpu.Params[3] = lodView.ZNear;
		gpu.AssetDispatch = static_cast<uint32_t>(frame.Assets.size() - 1);
		gpu.VisibleSegment = assetEntry.FirstInstance;
		gpu.SlotBatchBase = static_cast<uint32_t>(frame.SlotBatches.size());
		// Same condition as CullClusters: setting, uniform non-mirrored scale with single-sided materials, perspective.
		const bool coneCulling = mSettings.ConeCulling && candidate.Transform.ConeCulling && !input.View.Orthographic;
		gpu.Flags = (coneCulling ? VG_CULL_INSTANCE_CONE_CULLING : 0u) | (lodView.Orthographic ? VG_CULL_INSTANCE_LOD_ORTHOGRAPHIC : 0u);
		gpu.DrawInstance = instanceIndex;
		gpu.ClusterCount = static_cast<uint32_t>(asset->clusters().size());
		gpu.SlotCount = asset->materialSlotCount();
		frame.Instances.push_back(gpu);

		for (uint32_t slot = 0; slot < asset->materialSlotCount(); ++slot) {
			auto& batch = findBatch(candidate, slot);
			if (!batch.Instances) {
				batch.Segment = segments++;
			}
			++batch.Instances;
			frame.SlotBatches.push_back(batch.Segment);
		}

		VirtualGeometryGpuFrameRecord::Instance recorded;
		recorded.OwnerId = candidate.OwnerId;
		recorded.Asset = asset;
		recorded.Transform = candidate.Transform;
		recorded.LodTransform = lodTransform;
		recorded.LodView = lodView;
		recorded.Threshold = threshold;
		record.Instances.push_back(std::move(recorded));
	}

	// Segment capacity: proven per-slot cut bound x instances of the batch (see ComputeSlotCutBounds).
	frame.Segments.assign(segments, {});
	uint64_t drawCapacity = 0;
	for (auto& [key, batch] : mBatches) {
		const uint64_t capacity = uint64_t(batch.Asset->cutBounds().Bound[batch.Slot]) * batch.Instances;
		if (capacity > features.MaxDrawIndirectCount) {
			std::cerr << "VG_SELECTION_GPU_LIMIT segment capacity " << capacity << " exceeds maxDrawIndirectCount" << std::endl;
			return false;
		}
		batch.FirstDraw = static_cast<uint32_t>(drawCapacity);
		batch.DrawCount = static_cast<uint32_t>(capacity);
		frame.Segments[batch.Segment] = { batch.FirstDraw, batch.DrawCount };
		drawCapacity += capacity;
	}
	if (drawCapacity > UINT32_MAX / sizeof(RENDER::DrawIndexedIndirectCommand)) {
		std::cerr << "VG_SELECTION_GPU_LIMIT draw capacity " << drawCapacity << " exceeds the indirect buffer range" << std::endl;
		return false;
	}
	for (const auto& asset : frame.Assets) {
		const uint64_t groupsX = (asset.Asset->clusters().size() + 63) / 64;
		if (asset.InstanceCount > features.MaxComputeWorkGroupCountY || groupsX > features.MaxComputeWorkGroupCountX) {
			std::cerr << "VG_SELECTION_GPU_LIMIT asset " << asset.Asset->path() << " instances=" << asset.InstanceCount
					  << " exceed the compute dispatch limits" << std::endl;
			return false;
		}
	}
	frame.DrawCapacity = static_cast<uint32_t>(drawCapacity);

	auto& view = frame.View;
	view = {};
	view.FrustumCulling = mSettings.FrustumCulling && input.View.Frustum ? 1u : 0u;
	if (input.View.Frustum) {
		for (size_t plane = 0; plane < 6; ++plane) {
			const auto values = input.View.Frustum->getPlane(plane);
			std::copy(values.begin(), values.end(), view.Planes[plane]);
		}
	}
	view.Camera[0] = input.View.CameraPosition.x;
	view.Camera[1] = input.View.CameraPosition.y;
	view.Camera[2] = input.View.CameraPosition.z;
	return true;
}

void VirtualGeometryRenderer::applyGpuStatistics(const VirtualGeometryGpuStatistics& statistics, uint32_t latency) {
	mStatistics.GpuStatisticsValid = true;
	mStatistics.GpuStatisticsLatency = latency;
	mStatistics.InstancesCulled = statistics.InstancesCulled;
	mStatistics.LodClusters = statistics.LodClusters;
	mStatistics.LodTriangles = statistics.LodTriangles;
	mStatistics.VisibleClusters = statistics.VisibleClusters;
	mStatistics.VisibleTriangles = statistics.VisibleTriangles;
	mStatistics.FrustumCulledClusters = statistics.FrustumCulled;
	mStatistics.ConeCulledClusters = statistics.ConeCulled;
	mStatistics.OverflowDraws = statistics.Overflow;
	for (uint32_t level = 0; level < 8; ++level) {
		mStatistics.LodHistogram[level] = statistics.LodHistogram[level];
	}
}

void VirtualGeometryRenderer::uploadFrame(RENDER::Renderer& renderer) {
	if (mActivePath == VirtualGeometrySelectionPath::GPU) {
		// Draw commands and records are written by vgClusterCull; only the instance table comes from the CPU.
		if (!mInstanceBuffer || mInstances.size() > mInstanceCapacity) {
			mInstanceCapacity = GrowCapacity(mInstanceCapacity, mInstances.size());
			mInstanceBuffer = renderer.createStorageBuffer(nullptr, mInstanceCapacity, sizeof(InstanceGpu), RENDER::BufferUsage::STORAGE);
		}
		UploadChunked(*mInstanceBuffer, mInstances.data(), mInstances.size() * sizeof(InstanceGpu));
		return;
	}
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
	if (mBatches.empty() || !mClusterMaterial || (!mDrawCount && !mGpuDrawActive)) {
		return;
	}
	const auto& drawRecords = mGpuDrawActive ? mGpuSelection.drawRecordBuffer() : mDrawRecordBuffer;
	const auto& drawIndexStream = mGpuDrawActive ? mGpuSelection.drawIndexStream() : mDrawIndexStream;
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
			renderer.setStorageBuffer(DrawBufferName, drawRecords);
		}
		const auto& gpu = batch.Asset->gpuBuffers();
		renderer.execute([driver, layout = mLayout, vertices = gpu.Vertices, indices = gpu.Indices, drawIndex = drawIndexStream]() {
			driver->setVertexStreams(layout, { { ClusterVertexBinding, vertices }, { DrawIndexBinding, drawIndex } });
			driver->setIndexBuffer(indices);
		});
		renderer.setPrimitiveMode(RENDER::PrimitiveMode::TRIANGLES);
		if (mGpuDrawActive) {
			// One draw per batch: the GPU wrote the segment and its counter; maxDrawCount is the segment capacity.
			renderer.drawIndexedIndirectCount(mGpuSelection.indirectBuffer(), size_t(batch.FirstDraw) * stride, mGpuSelection.segmentCountBuffer(),
				size_t(batch.Segment) * sizeof(uint32_t), drawCount, stride);
			++mStatistics.IndirectCalls;
			mClusterMaterial->unbind();
			continue;
		}
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
