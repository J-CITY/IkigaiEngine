#include "backends/interface/storageBufferInterface.h"
#include "backends/interface/uniformBufferInterface.h"
#include "coreModule/core/core.h"
#include "gameRenderer.h"
#include "instanceBatch.h"
#include "render.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include <cstring>
#include <iostream>
#include <set>
#include <unordered_map>

namespace IKIGAI::RENDER {
namespace {
	void hashBytes(uint64_t& hash, const void* ptr, size_t size) {
		const auto* bytes = static_cast<const uint8_t*>(ptr);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
	}
	struct TriangleData {
		MATH::Vector4f P0, P1, P2, N0, N1, N2, Albedo, Properties, UV01, UV2;
	};
	static_assert(sizeof(TriangleData) == 160);
	struct SkinVertex {
		MATH::Vector4f Position, Normal, UV;
		std::array<int32_t, 4> BoneIds;
		MATH::Vector4f Weights;
	};
	struct DeformedVertex {
		MATH::Vector4f Position, Normal, UV;
	};
	struct TriangleUniform {
		MATH::Matrix4f World;
		MATH::Vector4f Albedo, Properties, UVTransform;
	};
	static_assert(sizeof(SkinVertex) == 80 && sizeof(DeformedVertex) == 48 && sizeof(TriangleUniform) == 112);
	void dispatchCompute(DriverInterface& driver, const std::shared_ptr<RayTracingProgram>& program, uint32_t count,
		std::map<std::string, std::vector<RayTracingResource>> bindings, const std::array<uint32_t, 4>& options) {
		RayTracingDispatch dispatch;
		dispatch.Program = program;
		dispatch.Extent = { count, 1, 1 };
		dispatch.Bindings = std::move(bindings);
		dispatch.PushConstants.resize(sizeof(options));
		std::memcpy(dispatch.PushConstants.data(), options.data(), sizeof(options));
		RenderStatistics::Get().recordDispatch();
		driver.dispatchComputeProgram(dispatch);
	}
}

std::shared_ptr<RayTracingProgram> LoadRayTracingProgram(Renderer& renderer, const std::string& path) {
	return renderer.getDriver()->createRayTracingProgram(RESOURCES::ShaderLoader::LoadProgramResource(path));
}

struct GameRenderer::RayScene {
	struct GeometrySource {
		std::shared_ptr<MeshInterface> Mesh;
		std::shared_ptr<StorageBufferInterface> Vertices, TriangleIndices;
		std::shared_ptr<VertexBufferInterface> RigidVertices;
		uint32_t VertexCount = 0, TriangleCount = 0;
	};
	struct MeshEntry {
		std::shared_ptr<MeshInterface> Mesh;
		std::shared_ptr<AccelerationStructure> AS;
		std::shared_ptr<VertexBufferInterface> DeformedBuffer;
		std::shared_ptr<GeometrySource> Source;
		uint64_t PoseHash = 0;
	};
	using Key = std::pair<const MeshInterface*, const SKELETON::AnimOffset*>;
	std::map<Key, MeshEntry> Meshes;
	std::map<const MeshInterface*, std::shared_ptr<GeometrySource>> Sources;
	std::shared_ptr<RayTracingProgram> SkinProgram, TriangleProgram;
	std::vector<RayTracingInstance> Instances;
};

void GameRenderer::prepareRayTracingScene(const std::vector<RenderChunk>& chunks) {
	auto& renderer = *mContext.render;
	if (!renderer.getDriver()->rayTracingCapabilities().AccelerationStructure) {
		throw UnsupportedRayTracing("RT scene extraction is unavailable on this backend");
	}
	if (!mRayScene) {
		mRayScene = std::make_shared<RayScene>();
		mRayScene->SkinProgram
			= renderer.getDriver()->createComputeProgram(RESOURCES::ShaderLoader::LoadProgramResource("/shaders/raytracing/skinVertices.shader"));
		mRayScene->TriangleProgram
			= renderer.getDriver()->createComputeProgram(RESOURCES::ShaderLoader::LoadProgramResource("/shaders/raytracing/buildTriangles.shader"));
	}
	mRaySceneChanged = false;
	if (!mRayWhite) {
		TextureResource white;
		white.width = white.height = 1;
		white.channels = 4;
		white.colorData = { 255, 255, 255, 255 };
		white.useMipmap = false;
		mRayWhite = renderer.createTexture(white);
	}
	std::vector<std::shared_ptr<TextureInterface>> albedoMaps;
	uint64_t hash = 1469598103934665603ull;
	uint32_t triangleCount = 0;
	std::vector<std::array<uint32_t, 4>> offsets;
	struct TriangleBatch {
		std::shared_ptr<VertexBufferInterface> Vertices;
		std::shared_ptr<RayScene::GeometrySource> Source;
		TriangleUniform Uniform;
		uint32_t Base;
	};
	std::vector<TriangleBatch> batches;
	std::vector<RayTracingInstance> instances;
	std::set<RayScene::Key> active;
	unsigned skinned = 0, builds = 0, refits = 0, skinDispatches = 0;
	auto visit = [&](const auto& drawables) {
		for (const auto& [distance, drawable] : drawables) {
			const auto mesh = drawable.mesh;
			if (!mesh || mesh->rayTracingVertices().empty()) {
				continue;
			}
			if (!mesh->morphTargets().empty()) {
				throw UnsupportedRayTracing("Ray tracing does not support morph target geometry");
			}
			const auto* pose = skinningPose(drawable);
			RayScene::Key key { mesh.get(), pose ? drawable.mAnimOffset : nullptr };
			active.insert(key);
			uint64_t poseHash = 1469598103934665603ull;
			if (pose) {
				hashBytes(poseHash, pose->Current.data(), sizeof(pose->Current));
				++skinned;
			}
			auto found = mRayScene->Meshes.find(key);
			if (found == mRayScene->Meshes.end()) {
				found = mRayScene->Meshes.emplace(key, RayScene::MeshEntry {}).first;
			}
			auto& entry = found->second;
			entry.Mesh = mesh;
			if (!entry.Source) {
				auto& source = mRayScene->Sources[mesh.get()];
				if (!source) {
					source = std::make_shared<RayScene::GeometrySource>();
					source->Mesh = mesh;
					std::vector<SkinVertex> input;
					std::vector<DeformedVertex> rigid;
					for (const auto& vertex : mesh->rayTracingVertices()) {
						const MATH::Vector4f p { vertex.position.x, vertex.position.y, vertex.position.z, 1 },
							n { vertex.normal.x, vertex.normal.y, vertex.normal.z, 0 }, uv { vertex.texCoord.x, vertex.texCoord.y, 0, 0 };
						std::array<int32_t, 4> boneIds {};
						for (size_t j = 0; j < 4; ++j) {
							boneIds[j] = static_cast<int32_t>(vertex.m_BoneIDs[j]);
						}
						input.push_back({ p, n, uv, boneIds, vertex.m_Weights });
						rigid.push_back({ p, n, uv });
					}
					source->VertexCount = static_cast<uint32_t>(input.size());
					source->Vertices = renderer.createStorageBuffer(input.data(), input.size(), sizeof(SkinVertex));
					source->RigidVertices = renderer.getDriver()->createRayTracingVertexBuffer(rigid.data(), rigid.size(), sizeof(DeformedVertex));
					std::vector<std::array<uint32_t, 4>> indices;
					const auto& original = mesh->rayTracingIndices();
					const auto count = original.empty() ? input.size() : original.size();
					for (size_t i = 0; i + 2 < count; i += 3) {
						const std::array<uint32_t, 4> row { original.empty() ? static_cast<uint32_t>(i) : original[i],
							original.empty() ? static_cast<uint32_t>(i + 1) : original[i + 1],
							original.empty() ? static_cast<uint32_t>(i + 2) : original[i + 2], 0 };
						for (size_t j = 0; j < 3; ++j) {
							if (row[j] >= input.size()) {
								throw std::runtime_error("RT triangle index exceeds vertex count");
							}
						}
						indices.push_back(row);
					}
					source->TriangleCount = static_cast<uint32_t>(indices.size());
					if (indices.empty()) {
						throw std::runtime_error("RT mesh needs triangle geometry");
					}
					source->TriangleIndices = renderer.createStorageBuffer(indices.data(), indices.size(), sizeof(indices[0]));
				}
				entry.Source = source;
			}
			if (pose && (!entry.DeformedBuffer || entry.PoseHash != poseHash)) {
				if (!entry.DeformedBuffer) {
					entry.DeformedBuffer
						= renderer.getDriver()->createRayTracingVertexBuffer(nullptr, entry.Source->VertexCount, sizeof(DeformedVertex));
				}
				dispatchCompute(*renderer.getDriver(), mRayScene->SkinProgram, entry.Source->VertexCount,
					{ { "0:0", { entry.Source->Vertices } }, { "0:1", { pose->CurrentUbo } }, { "0:2", { entry.DeformedBuffer } } },
					{ entry.Source->VertexCount, 0, 0, 0 });
				++skinDispatches;
				entry.PoseHash = poseHash;
				if (entry.AS) {
					renderer.getDriver()->refitBottomLevelAS(entry.AS);
					++refits;
				}
			}
			if (!entry.AS) {
				RayTracingGeometry geometry;
				geometry.Vertices = pose ? entry.DeformedBuffer : entry.Source->RigidVertices;
				geometry.Indices = mesh->rayTracingIndexBuffer();
				geometry.VertexCount = entry.Source->VertexCount;
				geometry.VertexStride = sizeof(DeformedVertex);
				geometry.IndexCount = static_cast<uint32_t>(mesh->getIndexCount());
				geometry.Opaque = false;
				entry.AS = renderer.getDriver()->createBottomLevelAS({ geometry }, pose != nullptr);
				++builds;
			}
			MATH::Vector4f albedo { .7f, .7f, .7f, 1 };
			float roughness = .5f, metallic = 0;
			auto albedoMap = mRayWhite;
			MATH::Vector2f tiling { 1, 1 }, offset { 0, 0 };
			if (drawable.material) {
				if (!drawable.material->pbrLayers().Layers.empty()) {
					throw std::runtime_error("Ray tracing does not yet support layered PBR textures: " + drawable.material->getPath());
				}
				for (const auto* name : { "data_UBOu_Albedo", "data_UBOu_Diffuse", "u_Diffuse" }) {
					if (auto* v = std::get_if<MATH::Vector4f>(drawable.material->findUniform(name))) {
						albedo = *v;
						break;
					}
				}
				if (auto* v = std::get_if<float>(drawable.material->findUniform("data_UBOu_Roughness"))) {
					roughness = *v;
				}
				if (auto* v = std::get_if<float>(drawable.material->findUniform("data_UBOu_Metallic"))) {
					metallic = *v;
				}
				for (const auto* name : { "u_AlbedoMap", "u_DiffuseMap" }) {
					if (auto* v = std::get_if<std::shared_ptr<TextureInterface>>(drawable.material->findUniform(name)); v && *v) {
						albedoMap = *v;
						break;
					}
				}
				if (auto* v = std::get_if<MATH::Vector2f>(drawable.material->findUniform("data_UBOu_TextureTiling"))) {
					tiling = *v;
				}
				if (auto* v = std::get_if<MATH::Vector2f>(drawable.material->findUniform("data_UBOu_TextureOffset"))) {
					offset = *v;
				}
			}
			const auto* identity = mesh.get();
			hashBytes(hash, &identity, sizeof(identity));
			hashBytes(hash, &key.second, sizeof(key.second));
			hashBytes(hash, &poseHash, sizeof(poseHash));
			hashBytes(hash, &drawable.world, sizeof(drawable.world));
			albedo.x *= drawable.tint.x;
			albedo.y *= drawable.tint.y;
			albedo.z *= drawable.tint.z;
			hashBytes(hash, &albedo, sizeof(albedo));
			hashBytes(hash, &roughness, sizeof(roughness));
			hashBytes(hash, &metallic, sizeof(metallic));
			const auto* textureIdentity = albedoMap.get();
			const auto revision = albedoMap->getRevision();
			hashBytes(hash, &textureIdentity, sizeof(textureIdentity));
			hashBytes(hash, &revision, sizeof(revision));
			hashBytes(hash, &tiling, sizeof(tiling));
			hashBytes(hash, &offset, sizeof(offset));
			albedoMaps.push_back(albedoMap);
			offsets.push_back({ triangleCount, 0, 0, 0 });
			batches.push_back({ pose ? entry.DeformedBuffer : entry.Source->RigidVertices, entry.Source,
				{ MATH::Matrix4f::Transpose(drawable.world), albedo, { roughness, metallic, 0, 0 }, { tiling.x, tiling.y, offset.x, offset.y } },
				triangleCount });
			triangleCount += entry.Source->TriangleCount;
			RayTracingInstance instance;
			instance.BottomLevel = found->second.AS;
			instance.Transform = drawable.world;
			instance.CustomIndex = static_cast<uint32_t>(instances.size());
			instances.push_back(std::move(instance));
		}
	};
	std::vector<std::pair<float, Drawable>> stableDrawables;
	for (const auto& chunk : chunks) {
		for (const auto* list : { &chunk.opaqueDrawablesForward, &chunk.opaqueDrawablesDeferred }) {
			for (const auto& item : *list) {
				if (item.second.instances) {
					for (const auto& transform : item.second.instances->transforms()) {
						auto single = item.second;
						single.instances.reset();
						single.world = transform.World;
						single.prevWorld = transform.PreviousWorld;
						single.tint = transform.Tint;
						stableDrawables.emplace_back(0, std::move(single));
					}
				}
				else {
					stableDrawables.emplace_back(0, item.second);
				}
			}
		}
	}
	std::sort(stableDrawables.begin(), stableDrawables.end(), [](const auto& a, const auto& b) {
		if (a.second.mesh.get() != b.second.mesh.get()) {
			return std::less<const MeshInterface*> {}(a.second.mesh.get(), b.second.mesh.get());
		}
		if (a.second.material.get() != b.second.material.get()) {
			return std::less<const MaterialInterface*> {}(a.second.material.get(), b.second.material.get());
		}
		if (a.second.mAnimOffset != b.second.mAnimOffset) {
			return std::less<const SKELETON::AnimOffset*> {}(a.second.mAnimOffset, b.second.mAnimOffset);
		}
		return std::memcmp(&a.second.world, &b.second.world, sizeof(a.second.world)) < 0;
	});
	visit(stableDrawables);
	if (albedoMaps.size() > 64) {
		throw std::runtime_error("Default RT material provider supports 64 instances; bind a custom scene provider for larger scenes");
	}
	for (auto i = mRayScene->Meshes.begin(); i != mRayScene->Meshes.end();) {
		if (!active.contains(i->first)) {
			i = mRayScene->Meshes.erase(i);
		}
		else {
			++i;
		}
	}
	for (auto i = mRayScene->Sources.begin(); i != mRayScene->Sources.end();) {
		const bool used = std::any_of(active.begin(), active.end(), [&](const auto& key) { return key.first == i->first; });
		if (!used) {
			i = mRayScene->Sources.erase(i);
		}
		else {
			++i;
		}
	}
	// The geometry table is an optional default provider. Custom programs can
	// bind their own buffers instead; it is not part of trace dispatch semantics.
	if (hash != mRaySceneHash || !mRayTriangles) {
		mRaySceneChanged = true;
		if (offsets.empty()) {
			offsets.resize(1);
		}
		TriangleData empty {};
		mRayTriangles = renderer.createStorageBuffer(triangleCount ? nullptr : &empty, std::max<uint32_t>(1, triangleCount), sizeof(TriangleData));
		for (const auto& batch : batches) {
			auto uniform = renderer.createUniformBuffer<TriangleUniform>(batch.Uniform);
			dispatchCompute(*renderer.getDriver(), mRayScene->TriangleProgram, batch.Source->TriangleCount,
				{ { "0:0", { batch.Vertices } }, { "0:1", { batch.Source->TriangleIndices } }, { "0:2", { uniform } }, { "0:3", { mRayTriangles } } },
				{ batch.Source->VertexCount, batch.Source->TriangleCount, batch.Base, 0 });
		}
		mRayInstances = renderer.createStorageBuffer(offsets.data(), offsets.size(), sizeof(offsets[0]));
		if (mRayScene->Instances.size() != instances.size()) {
			mRayTopLevels.clear();
		}
		mRayScene->Instances = std::move(instances);
		mRaySceneHash = hash;
		for (auto& [key, top] : mRayTopLevels) {
			auto updated = mRayScene->Instances;
			for (auto& instance : updated) {
				instance.HitGroupOffset = static_cast<uint32_t>(std::stoul(key));
			}
			renderer.getDriver()->refitTopLevelAS(top, updated);
		}
		mRayAlbedoMaps = std::move(albedoMaps);
		mRayAlbedoMaps.resize(64, mRayWhite);
		mRenderPipeline->mHistoryValid = false;
		mFrameData.FrameState.x = 0;
		mFrameUbo->setData(mFrameData);
		std::cout << "RAY_TRACING_SCENE instances=" << mRayScene->Instances.size() << " triangles=" << triangleCount << " skinned=" << skinned
				  << " blas_builds=" << builds << " blas_refits=" << refits << " tlas_refits=" << mRayTopLevels.size()
				  << " skin_compute=" << skinDispatches << " triangle_compute=" << batches.size() << "\n";
	}
}

void GameRenderer::runRayTracingStage(PipelineStage& stage) {
	const auto& desc = stage.mDescriptor;
	if (!stage.mRayTracingProgram) {
		for (const auto& [output, input] : desc.Bypass) {
			mRenderPipeline->mTextures.at(output) = mRenderPipeline->mTextures.at(input);
		}
		return;
	}
	RayTracingDispatch dispatch;
	dispatch.Program = stage.mRayTracingProgram;
	dispatch.Extent = { static_cast<uint32_t>(desc.LaunchX), static_cast<uint32_t>(desc.LaunchY), static_cast<uint32_t>(desc.LaunchZ) };
	if (!desc.LaunchTexture.empty()) {
		const auto& target = mRenderPipeline->mTextures.at(desc.LaunchTexture);
		dispatch.Extent = { static_cast<uint32_t>(target->getWidth()), static_cast<uint32_t>(target->getHeight()),
			static_cast<uint32_t>(std::max<size_t>(1, target->getDepth())) };
	}
	dispatch.RaygenRecord = static_cast<uint32_t>(desc.RaygenRecord);
	for (int b : desc.PushConstants) {
		if (b < 0 || b > 255) {
			throw std::runtime_error("RT PushConstants must contain bytes");
		}
		dispatch.PushConstants.push_back(static_cast<uint8_t>(b));
	}
	const MATH::Vector4f rayState { static_cast<float>(mRaySample), mRenderPipeline->mHistoryValid ? 1.f : 0.f, static_cast<float>(mFrameCount),
		mRaySceneChanged ? 0.f : 1.f };
	auto& renderer = *mContext.render;
	auto rayFrame = renderer.createUniformBuffer<MATH::Vector4f>(rayState);
	auto resource = [&](const std::string& source) -> RayTracingResource {
		if (source == "@Scene") {
			if (!mRayScene) {
				throw std::runtime_error("No RT scene snapshot");
			}
			const auto key = std::to_string(desc.HitGroupOffset);
			if (!mRayTopLevels.contains(key)) {
				auto instances = mRayScene->Instances;
				for (auto& instance : instances) {
					instance.HitGroupOffset = static_cast<uint32_t>(desc.HitGroupOffset);
				}
				mRayTopLevels[key] = renderer.getDriver()->createTopLevelAS(instances, true);
			}
			return mRayTopLevels.at(key);
		}
		if (source == "@Triangles") {
			return mRayTriangles;
		}
		if (source == "@Instances") {
			return mRayInstances;
		}
		if (source == "@RayFrame") {
			return rayFrame;
		}
		if (source == "engine_UBO") {
			return mEngineUbo;
		}
		if (source == "engine_Frame") {
			return mFrameUbo;
		}
		if (source == "engine_Lights") {
			return mRayLightSSBO;
		}
		if (source == "@VisibleLights") {
			return mLightSSBO;
		}
		if (source == "engine_CurrentShadow") {
			return mCurrentShadowUbo;
		}
		if (source.rfind("AS:", 0) == 0) {
			return mRenderPipeline->mAccelerationStructures.at(source.substr(3));
		}
		if (mRenderPipeline->mAccelerationStructures.contains(source)) {
			return mRenderPipeline->mAccelerationStructures.at(source);
		}
		if (mRenderPipeline->mTextures.contains(source)) {
			return mRenderPipeline->mTextures.at(source);
		}
		if (mRenderPipeline->mStorageBuffers.contains(source)) {
			return mRenderPipeline->mStorageBuffers.at(source);
		}
		if (mRenderPipeline->mUniformBuffers.contains(source)) {
			return mRenderPipeline->mUniformBuffers.at(source);
		}
		throw std::runtime_error("Unknown RT pipeline binding: " + source);
	};
	// Array bindings use comma-separated resource names; each element resolves
	// to a strong reference in the submitted immutable dispatch snapshot.
	for (const auto& [name, sources] : desc.Bindings) {
		if (sources == "@AlbedoMaps") {
			for (const auto& texture : mRayAlbedoMaps) {
				dispatch.Bindings[name].push_back(texture);
			}
			continue;
		}
		size_t begin = 0;
		do {
			const auto end = sources.find(',', begin);
			dispatch.Bindings[name].push_back(resource(sources.substr(begin, end == std::string::npos ? end : end - begin)));
			if (end == std::string::npos) {
				break;
			}
			begin = end + 1;
		} while (true);
	}
	if (desc.Draw == DrawContent::COMPUTE) {
		renderer.dispatchRayQueries(std::move(dispatch));
	}
	else {
		renderer.traceRays(std::move(dispatch));
	}
	stage.mExecuted = true;
	if (mFrameCount == 0) {
		std::cout << (desc.Draw == DrawContent::COMPUTE ? "Pipeline ray query: " : "Pipeline ray trace: ") << stage.mName << "\n";
	}
}
}
