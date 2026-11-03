#pragma once
#include "backends/interface/gpuFeatures.h"
#include "backends/interface/materialInterface.h"
#include "backends/interface/rayTracingInterface.h"
#include "backends/interface/resourceStruct.h"
#include "backends/interface/upscalerInterface.h"
#include "drawable.h"
#include "environmentModule/atmosphereCache.h"
#include "environmentModule/environment.h"
#include "environmentModule/environmentQuality.h"
#include "frustum.h"
#include "gameRendererInterface.h"
#include "historyValidity.h"
#include "indirectLighting/temporalDenoiseSettings.h"
#include "light.h"
#include "primitiveRender.h"
#include "profilerModule/gpuStageProfiler.h"
#include "renderGraph/computeStageDescriptor.h"
#include "renderGraph/screenProbeSettings.h"
#include <filesystem>

// TODO move include to .cpp
#ifdef OCULUS
#include "util_egl.h"
#include "util_oxr.h"
#endif

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::ECS {
class CameraComponent;
}
namespace IKIGAI::VECTOR {
class VectorRenderer;
}
namespace IKIGAI::RENDER {
class ParticleRenderer;
}
namespace IKIGAI::VG {
class VirtualGeometryRenderer;
}
#if USE_PHYSICS_3D
namespace IKIGAI::RENDER {
class DeformableRenderer;
}
#endif

namespace IKIGAI {
namespace CORE {
	class Core;
}

}
namespace IKIGAI::RENDER {
class MaterialInterface;
class FrameBufferInterface;
class TextureInterface;
class UniformBufferInterface;
class StorageBufferInterface;
class Renderer;
struct ComputeStageState;

enum class DrawContent {
	FORWARD = 0,
	DEFERRED,
	GUI,
	QUAD,
	SHADOW,
	TRANSPARENT_GEOMETRY,
	COMPUTE,
	CLEAR,
	LIGHT_ACCUMULATION,
	RAY_TRACE,
	UPSCALE,
	FRAME_GENERATION,
	VECTOR_WORLD,
	PRIMITIVES_WORLD,
	PRIMITIVES_SCREEN,
	WEATHER
};

struct PipelineStage {
	using UniformType
		= std::variant<float, int, bool, MATH::Vector2f, MATH::Vector3f, MATH::Vector4f, std::shared_ptr<::IKIGAI::RENDER::TextureInterface>>;

	struct Descriptor {
		using UniformTypeDescr = std::variant<float, int, bool, MATH::Vector4f, MATH::Vector3f, MATH::Vector2f, std::string>;

		std::string Name;
		DrawContent Draw;
		std::string Material;
		std::string FrameBuffer;
		std::string Program;
		std::string LaunchTexture;
		int LaunchX = 1, LaunchY = 1, LaunchZ = 1;
		int RaygenRecord = 0;
		std::map<std::string, std::string> Bindings;
		std::vector<std::string> Reads, Writes;
		std::vector<int> PushConstants;
		std::string Unsupported = "ERROR";
		int HitGroupOffset = 0;
		bool Progressive = false;
		bool PresentationSink = false, IncludeEditorUI = false;
		std::map<std::string, UniformTypeDescr> Uniforms;
		bool Enabled = true;
		bool Once = false;
		int UpdateInterval = 1;
		std::string DispatchTexture;
		bool ClearColor = true;
		bool ClearDepth = true;
		MATH::Vector4f ClearValue = { 0.0f, 0.0f, 0.0f, 1.0f };
		bool OverrideMaterial = false;
		std::string ShadowType;
		float ShadowRange = 100.0f;
		int ShadowFace = -1;
		int ShadowLightId = -1;
		std::string LightPass;
		bool Additive = false;
		std::map<std::string, std::string> Bypass;
		ComputeStageDescriptor Compute;

		std::map<std::string, std::string> BufferLinks; // ShaderBufferName -> Pipeline/GlobalBufferName
		std::map<std::string, std::map<std::string, UniformTypeDescr>> BufferOverrides; // BufferName -> MemberName -> Value

		template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
			using Self = Descriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::Draw, "Draw")
				.field(&Self::Material, "Material", default_ { std::string {} })
				.field(&Self::FrameBuffer, "FrameBuffer", default_ { "" })
				.field(&Self::Program, "Program", default_ { std::string {} })
				.field(&Self::LaunchTexture, "LaunchTexture", default_ { std::string {} })
				.field(&Self::LaunchX, "LaunchX", default_ { 1 })
				.field(&Self::LaunchY, "LaunchY", default_ { 1 })
				.field(&Self::LaunchZ, "LaunchZ", default_ { 1 })
				.field(&Self::RaygenRecord, "RaygenRecord", default_ { 0 })
				.field(&Self::Bindings, "Bindings", default_ { decltype(Bindings) {} })
				.field(&Self::Reads, "Reads", default_ { decltype(Reads) {} })
				.field(&Self::Writes, "Writes", default_ { decltype(Writes) {} })
				.field(&Self::PushConstants, "PushConstants", default_ { decltype(PushConstants) {} })
				.field(&Self::Unsupported, "Unsupported", default_ { std::string { "ERROR" } })
				.field(&Self::HitGroupOffset, "HitGroupOffset", default_ { 0 })
				.field(&Self::Progressive, "Progressive", default_ { false })
				.field(&Self::PresentationSink, "PresentationSink", default_ { false })
				.field(&Self::IncludeEditorUI, "IncludeEditorUI", default_ { false })
				.field(&Self::Uniforms, "Uniforms", default_ { decltype(Uniforms) {} })
				.field(&Self::BufferLinks, "BufferLinks", default_ { decltype(BufferLinks) {} })
				.field(&Self::BufferOverrides, "BufferOverrides", default_ { decltype(BufferOverrides) {} })
				.field(&Self::Once, "Once", default_ { false })
				.field(&Self::UpdateInterval, "UpdateInterval", default_ { 1 })
				.field(&Self::DispatchTexture, "DispatchTexture", default_ { std::string {} })
				.field(&Self::Enabled, "Enabled", default_ { true })
				.field(&Self::ClearColor, "ClearColor", default_ { true })
				.field(&Self::ClearDepth, "ClearDepth", default_ { true })
				.field(&Self::ClearValue, "ClearValue", default_ { MATH::Vector4f { 0, 0, 0, 1 } })
				.field(&Self::OverrideMaterial, "OverrideMaterial", default_ { false })
				.field(&Self::ShadowType, "ShadowType", default_ { std::string {} })
				.field(&Self::ShadowRange, "ShadowRange", default_ { 100.0f })
				.field(&Self::ShadowFace, "ShadowFace", default_ { -1 })
				.field(&Self::ShadowLightId, "ShadowLightId", default_ { -1 })
				.field(&Self::LightPass, "LightPass", default_ { std::string {} })
				.field(&Self::Additive, "Additive", default_ { false })
				.field(&Self::Bypass, "Bypass", default_ { decltype(Bypass) {} })
				.field(&Self::Compute, "Compute", default_ { ComputeStageDescriptor {} });
		}
	};
	PipelineStage() = default;
	Descriptor mDescriptor;
	bool mExecuted = false;
	std::shared_ptr<RayTracingProgram> mRayTracingProgram;
	std::shared_ptr<ComputeStageState> mCompute;

	std::string mName;
	std::shared_ptr<::IKIGAI::RENDER::FrameBufferInterface> mFrameBuffer;
	std::shared_ptr<::IKIGAI::RENDER::MaterialInterface> mMaterial;
	DrawContent mDrawContent;
	std::map<std::string, UniformType> mUniforms;

	std::map<std::string, std::string> mBufferLinks;
	std::map<std::string, std::map<std::string, UniformType>> mBufferOverrides;
	std::map<std::string, std::shared_ptr<UniformBufferInterface>> mOverrideBuffers;
	std::map<std::string, MaterialInterface::UniformData> mDefaults;
};

struct RenderGraphPipeline {
	struct TextureDesc {
		enum class SizeBase { DISPLAY, RENDER, FIXED };
		std::string Name;
		std::string Path;
		TextureResource Resource;
		float Scale = 1.0f;
		SizeBase Size = SizeBase::DISPLAY;
		bool History = false;
		bool Persistent = false;
		// Allocates floor(log2(max(width, height))) + 1 mips for the resolved size (render-size pyramids such as HZB).
		// Requires UseMipmap false: the chain is filled by compute stages, not by blitting.
		bool FullMipChain = false;

		template <class Context> constexpr static auto serde(Context& context, TextureDesc& value) {
			using Self = TextureDesc;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::Path, "Path", default_ { std::string {} })
				.field(&Self::Resource, "Resource", default_ { TextureResource {} })
				.field(&Self::Scale, "Scale", default_ { 1.0f })
				.field(&Self::Size, "Size", default_ { SizeBase::DISPLAY })
				.field(&Self::History, "History", default_ { false })
				.field(&Self::Persistent, "Persistent", default_ { false })
				.field(&Self::FullMipChain, "FullMipChain", default_ { false });
		}
	};

	struct FrameDesc {
		std::string Name;
		std::vector<std::string> Textures;
		std::string Depth;
		unsigned Mip = 0;
		unsigned Layer = 0;

		template <class Context> constexpr static auto serde(Context& context, FrameDesc& value) {
			using Self = FrameDesc;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::Textures, "Textures")
				.field(&Self::Depth, "Depth", default_ { "" })
				.field(&Self::Mip, "Mip", default_ { 0u })
				.field(&Self::Layer, "Layer", default_ { 0u });
		}
	};

	enum class BufferType { UNIFORM = 0, STORAGE };

	struct BufferDescriptor {
		std::string Name;
		size_t Size = 0;
		size_t Stride = 0; // Needed for SSBO
		size_t Count = 0; // SSBO element count; alternative to the legacy Size
		BufferType Type = BufferType::UNIFORM;
		std::vector<PipelineBufferUsage> Usage;

		template <class Context> constexpr static auto serde(Context& context, BufferDescriptor& value) {
			using Self = BufferDescriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::Size, "Size", default_ { 0 })
				.field(&Self::Stride, "Stride", default_ { 0 })
				.field(&Self::Type, "Type", default_ { BufferType::UNIFORM })
				.field(&Self::Count, "Count", default_ { size_t { 0 } })
				.field(&Self::Usage, "Usage", default_ { std::vector<PipelineBufferUsage> {} });
		}
	};

	struct SettingsDescriptor {
		struct Group {
			std::vector<std::string> Materials;
			std::map<std::string, PipelineStage::Descriptor::UniformTypeDescr> Uniforms;
			std::map<std::string, std::map<std::string, std::string>> Rename;
			template <class Context> constexpr static auto serde(Context& context, Group& value) {
				using Self = Group;
				serde::serde_struct(context, value)
					.field(&Self::Materials, "Materials")
					.field(&Self::Uniforms, "Uniforms")
					.field(&Self::Rename, "Rename", serde::attribute::default_ { decltype(Rename) {} });
			}
		};
		std::vector<TextureDesc> Textures;
		std::map<std::string, std::map<std::string, PipelineStage::Descriptor::UniformTypeDescr>> Materials;
		std::vector<Group> Groups;
		template <class Context> constexpr static auto serde(Context& context, SettingsDescriptor& value) {
			using Self = SettingsDescriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Textures, "Textures", default_ { decltype(Textures) {} })
				.field(&Self::Materials, "Materials", default_ { decltype(Materials) {} })
				.field(&Self::Groups, "Groups", default_ { decltype(Groups) {} });
		}
	};

	struct LightPassDescriptor {
		std::string Name;
		std::string ShadowProviderStage;
		int ShadowBudget = -1;
		int VolumeLightLimit = -1;
		std::vector<int> LightIds;
		std::vector<PipelineStage::Descriptor> InitStages, CaptureStages, PrepareStages, ResolveStages;
		template <class Context> constexpr static auto serde(Context& context, LightPassDescriptor& value) {
			using Self = LightPassDescriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::ShadowProviderStage, "ShadowProviderStage", default_ { std::string {} })
				.field(&Self::ShadowBudget, "ShadowBudget", default_ { -1 })
				.field(&Self::VolumeLightLimit, "VolumeLightLimit", default_ { -1 })
				.field(&Self::LightIds, "LightIds", default_ { decltype(LightIds) {} })
				.field(&Self::InitStages, "InitStages")
				.field(&Self::CaptureStages, "CaptureStages")
				.field(&Self::PrepareStages, "PrepareStages", default_ { decltype(PrepareStages) {} })
				.field(&Self::ResolveStages, "ResolveStages");
		}
	};
	struct Descriptor {
		std::string Name;
		std::string SettingsAsset;
		SettingsDescriptor Settings;
		bool TemporalJitter = false;
		std::vector<TextureDesc> Textures;
		std::vector<FrameDesc> FrameBuffers;
		std::vector<BufferDescriptor> Buffers;
		std::vector<PipelineStage::Descriptor> StartStages;
		std::vector<PipelineStage::Descriptor> Stages;
		std::vector<LightPassDescriptor> LightPasses;
		// Screen probe gather parameters (hybrid-sp); disabled unless the pipeline enables the block.
		ScreenProbeSettings ScreenProbes;
		// Temporal denoiser settings blocks (indirectLighting/temporalDenoiseSettings.h), one uniform buffer per entry.
		std::vector<TemporalDenoiseSettings> TemporalDenoisers;

		template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
			using Self = Descriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Self::Name, "Name")
				.field(&Self::SettingsAsset, "SettingsAsset", default_ { std::string {} })
				.field(&Self::Settings, "Settings", default_ { SettingsDescriptor {} })
				.field(&Self::TemporalJitter, "TemporalJitter", default_ { false })
				.field(&Self::Textures, "Textures", default_ { decltype(Textures) {} })
				.field(&Self::FrameBuffers, "FrameBuffers", default_ { decltype(FrameBuffers) {} })
				.field(&Self::Buffers, "Buffers", default_ { decltype(Buffers) {} })
				.field(&Self::Stages, "Stages")
				.field(&Self::LightPasses, "LightPasses", default_ { decltype(LightPasses) {} })
				.field(&Self::StartStages, "StartStages", default_ { decltype(StartStages) {} })
				.field(&Self::ScreenProbes, "ScreenProbes", default_ { ScreenProbeSettings {} })
				.field(&Self::TemporalDenoisers, "TemporalDenoisers", default_ { std::vector<TemporalDenoiseSettings> {} });
		}
	};

	RenderGraphPipeline() = default;
	RenderGraphPipeline(const Descriptor& descriptor, RENDER::Renderer& renderer, const std::string& pipelineAsset = {});

	std::map<std::string, std::shared_ptr<RENDER::FrameBufferInterface>> mFrameBuffers;
	std::map<std::string, std::shared_ptr<RENDER::FrameBufferInterface>> mAlternateFrameBuffers;
	std::map<std::string, std::shared_ptr<RENDER::TextureInterface>> mTextures;
	std::map<std::string, std::shared_ptr<RENDER::StorageBufferInterface>> mStorageBuffers;
	std::map<std::string, std::shared_ptr<RENDER::UniformBufferInterface>> mUniformBuffers;
	std::map<std::string, std::shared_ptr<AccelerationStructure>> mAccelerationStructures;
	std::map<std::string, BufferUsage> mBufferUsages;

	using StagesArr = std::list<std::unique_ptr<PipelineStage>>;

	StagesArr mStartStages;
	StagesArr mStages;
	struct LightPass {
		LightPassDescriptor Descriptor;
		StagesArr InitStages, CaptureStages, PrepareStages, ResolveStages;
	};
	std::map<std::string, LightPass> mLightPasses;
	std::vector<StagesArr*> stageGroups();

	void resize(Renderer& renderer, int width, int height);
	void prepareUpscaler(Renderer& renderer, int width, int height);
	void advanceHistory(Renderer& renderer);
	void rebuildFrameBuffers(Renderer& renderer);
	void reloadSettings(Renderer& renderer, const std::string& asset = {});
	void pollSettings(Renderer& renderer);
	void invalidateResources(
		const std::vector<std::string>& resources, bool resetHistory = true, HistoryReason reason = HistoryReason::EnvironmentChange);
	void pollResourceUpdates();
	Descriptor mDescriptor;
	Descriptor mSourceDescriptor;
	std::filesystem::file_time_type mSettingsWriteTime {};
	std::filesystem::file_time_type mPipelineWriteTime {};
	std::string mPipelineAsset;
	std::string mSettingsOverrideAsset;
	std::map<std::string, uint64_t> mTextureRevisions;
	MATH::Vector2i mSize = { 0, 0 };
	MATH::Vector2i mRenderSize = { 0, 0 }, mAllocatedRenderSize = { 0, 0 };
	std::unique_ptr<UpscalerInterface> mUpscaler;
	UpscalerSettings mUpscalerSettings;
	UpscalerCapabilities mUpscalerStatus;
	bool mUpscalerSettingsLoaded = false;
	bool mUpscalerFailed = false;
	// Per-effect temporal history validity (GI, reflections, shadows, presentation, upscaler).
	HistoryValidity mHistory;
	// Temporal denoiser buffers and light change state (indirectLighting/temporalDenoiseStage.h).
	TemporalDenoiseRuntime mTemporalDenoise;
	std::map<std::string, std::shared_ptr<TextureInterface>> mAllocatedTextures;

	// private:
	bool mIsInitialized = false;

	StagesArr loadStages(const std::vector<PipelineStage::Descriptor>& stages);
};

class GameRenderer : public RENDER::GameRendererInterface {
	friend struct RenderGraphPipeline;

	IKIGAI::CORE::Core& mContext;
	std::unique_ptr<RenderGraphPipeline> mRenderPipeline;
	HistoryCounters mRetiredHistoryCounters;
	HistoryCounters mLoggedHistoryCounters;
	std::string mStartupPipelineAsset, mActivePipelineAsset;
	std::weak_ptr<void> mRenderedScene;
	std::uint64_t mRenderedTopologyRevision = 0;

	std::shared_ptr<StorageBufferInterface> mLightSSBO;
	struct RayScene;
	std::shared_ptr<RayScene> mRayScene;
	std::map<std::string, std::shared_ptr<AccelerationStructure>> mRayTopLevels;
	std::shared_ptr<StorageBufferInterface> mRayTriangles, mRayInstances;
	std::shared_ptr<StorageBufferInterface> mRayLightSSBO;
	std::vector<LightOGL> mRayLights;
	std::vector<std::shared_ptr<TextureInterface>> mRayAlbedoMaps;
	std::shared_ptr<TextureInterface> mRayWhite;
	uint64_t mRayTopologyHash = 0;
	uint64_t mRayMaterialHash = 0;
	uint64_t mRayDynamicHash = 0;
	unsigned mRaySample = 0;
	// True when the RT scene topology, materials or lights changed. Pure dynamic
	// motion (skinning, rigid transforms) does not set it; see HistoryValidity::sceneMoved.
	bool mRaySceneChanged = true;
	std::shared_ptr<UniformBufferInterface> mRayFrameUbo;
	// Contents of the engine_RayFrame uniform block. Only the first vec4 existed
	// before M3; shaders declaring just {vec4 State;} keep working.
	struct RayFrameData {
		// x = sample index since the last RT reset, y = 1 when GI, reflection and shadow
		// history are all valid, z = frame counter, w = 0 when topology/material/lights changed.
		MATH::Vector4f State;
		// x = 1 when dynamic scene motion (skinning, rigid transforms) happened this frame,
		// y/z/w = per-effect history validity of GI, reflections and shadows.
		MATH::Vector4f Motion;
	};
	static_assert(sizeof(RayFrameData) == 32);
	RayFrameData rayFrameData() const;
	void prepareRayTracingScene(const std::vector<RenderChunk>& chunks);
	void runRayTracingStage(PipelineStage& stage);
	RayTracingDispatch buildProgramDispatch(PipelineStage& stage);
	void runComputeStage(PipelineStage& stage);
	std::vector<LightOGL> mLights;
	std::vector<int> mLightObjectIds;
	std::vector<float> mEnvironmentSun;
	std::map<std::string, MaterialInterface::UniformData> mOverrideDefaults;
	std::shared_ptr<RENDER::MaterialInterface> mEmptyMaterial;
	std::shared_ptr<RENDER::TextureInterface> mEmptyTexture;
	std::shared_ptr<RENDER::TextureInterface> mEmptyVolume;
	std::shared_ptr<UniformBufferInterface> mEngineUbo;
	std::shared_ptr<UniformBufferInterface> mEnvironmentUbo;
	std::shared_ptr<UniformBufferInterface> mSurfaceWeatherUbo;
	std::shared_ptr<UniformBufferInterface> mInteractionUbo;
	std::shared_ptr<UniformBufferInterface> mSurfaceUbo;
	std::shared_ptr<UniformBufferInterface> mInstanceMetaUbo = {};
	std::shared_ptr<StorageBufferInterface> mEmptyInstanceBuffer = {};
	ENVIRONMENT::GpuState mEnvironmentState = {};
	ENVIRONMENT::QualitySettings mEnvironmentQuality = {};
	MATH::Vector2f mCloudShadowAnchor = {};
	MATH::Vector2i mCloudShadowViewport = {};
	bool mCloudShadowAnchorValid = false;
	bool mCloudShadowNeedsUpdate = true;
	struct InteractionGpu {
		MATH::Vector4f Count = {};
		std::array<MATH::Vector4f, 32> StartRadius = {};
		std::array<MATH::Vector4f, 32> EndStrength = {};
		std::array<MATH::Vector4f, 32> PreviousStartRadius = {};
		std::array<MATH::Vector4f, 32> PreviousEndStrength = {};
		std::array<MATH::Vector4f, 32> Owners = {};
	} mInteractionState = {};
	ENVIRONMENT::AtmosphereCache mAtmosphereCache = {};
	PipelineStage* mAtmosphereTransmittanceStage = nullptr;
	PipelineStage* mAtmosphereSkyViewStage = nullptr;
	PipelineStage* mAtmosphereAerialStage = nullptr;
	void updateEnvironment(SCENE_SYSTEM::Scene& scene);
	void bindEnvironment(Renderer& renderer, const ShaderReflection& reflection);
	std::shared_ptr<UniformBufferInterface> mBoneUbo;
	std::shared_ptr<UniformBufferInterface> mMorphUbo;
	std::shared_ptr<StorageBufferInterface> mEmptyMorphBuffer;
	struct SkinningPose {
		std::shared_ptr<SKELETON::Skeleton> Skeleton;
		SKELETON::IAnimationPlayable* Playable = nullptr;
		std::array<MATH::Matrix4f, 128> Current, Previous;
		std::shared_ptr<UniformBufferInterface> CurrentUbo, PreviousUbo;
		unsigned Frame = 0;
	};
	std::map<const SKELETON::AnimOffset*, SkinningPose> mSkinningPoses;
	const SkinningPose* skinningPose(const Drawable& drawable);
	std::shared_ptr<UniformBufferInterface> mFrameUbo;
	std::shared_ptr<UniformBufferInterface> mDrawFrameUbo;
	std::shared_ptr<UniformBufferInterface> mShadowUbo;
	std::shared_ptr<UniformBufferInterface> mShadowPassUbo;
	std::shared_ptr<UniformBufferInterface> mPointShadowPassUbo;
	std::shared_ptr<MeshInterface> mFullscreenMesh;
	std::shared_ptr<MaterialInterface> mOverrideMaterial;
	std::map<std::tuple<ShaderInterface::Id, uint64_t, unsigned>, std::shared_ptr<MaterialInterface>> mPbrPassMaterials;
	PipelineStage* mActiveStage = nullptr;
	std::shared_ptr<MaterialInterface> mGuiSpriteMaterial;
	std::shared_ptr<MaterialInterface> mGuiTextMaterial;
	std::shared_ptr<MaterialInterface> mVectorMaterial, mSdfMaterial;
	std::shared_ptr<VECTOR::VectorRenderer> mVectorRenderer;
	PrimitiveRender mPrimitives;
	std::shared_ptr<ParticleRenderer> mParticleRenderer;
	std::shared_ptr<VG::VirtualGeometryRenderer> mVirtualGeometry;
#if USE_PHYSICS_3D
	std::shared_ptr<DeformableRenderer> mDeformableRenderer;
#endif
	MATH::Matrix4f mPreviousViewProjection = MATH::Matrix4f::Identity;
	MATH::Matrix4f mPreviousUnjitteredViewProjection = MATH::Matrix4f::Identity;
	MATH::Vector2f mJitter {}, mPreviousJitter {};
	const ECS::CameraComponent* mPreviousCamera = nullptr;
	void bindStage(PipelineStage& stage);
	void captureShadows(
		PipelineStage& stage, const std::vector<RenderChunk>& casters, const MATH::Vector3f& cameraPosition, const MATH::Vector2f& size);
	void renderGui(SCENE_SYSTEM::Scene& scene, bool worldVectors = false);

	EngineUBO uboData;
	struct FrameUBO {
		MATH::Matrix4f InvViewProjection;
		MATH::Matrix4f PreviousViewProjection;
		MATH::Matrix4f PreviousModel;
		MATH::Vector4f CameraParams; // near, far, depth in [0,1], texture Y direction
		MATH::Vector4f FrameState; // valid history, elapsed seconds, reserved, reserved
	} mFrameData {};
	struct ShadowUBO {
		MATH::Matrix4f Matrices[11]; // 4 directional cascades, spot, 6 point faces
		MATH::Vector4f CascadeSplits;
		MATH::Vector4f LightIndices = { -1, -1, -1, 0 };
		MATH::Vector4f PointPositionRange;
		MATH::Vector4f Settings = { 0.001f, 0, 0, 0 };
	} mShadowData {};
	struct PointShadowPassUBO {
		MATH::Matrix4f ViewProjection;
		MATH::Vector4f LightPositionFar;
		MATH::Vector4f ClipRange;
	};
	static_assert(sizeof(PointShadowPassUBO) == 96);
	struct CurrentShadowUBO {
		MATH::Matrix4f Matrices[4];
		MATH::Vector4f CascadeSplits;
		MATH::Vector4f PositionRange;
		MATH::Vector4f Settings;
		MATH::Vector4f LightInfo;
	} mCurrentShadow {};
	static_assert(sizeof(CurrentShadowUBO) == 320);
	std::shared_ptr<UniformBufferInterface> mCurrentShadowUbo;
	int mCaptureLightIndex = -1;

	unsigned long long mFrameCount = 0ull;
	PROFILER::GpuStageProfiler mGpuProfiler = {};

	void renderScene(IKIGAI::SCENE_SYSTEM::Scene& scene, IKIGAI::ECS::CameraComponent&);
	void updateLights(SCENE_SYSTEM::Scene& scene);
	void updateLightsInFrustum(SCENE_SYSTEM::Scene& scene, const Frustum& frustum);

	const RenderGraphPipeline& getCurrentPipeline() const override;

public:
	GameRenderer(CORE::Core& context);
	~GameRenderer() override;
	PrimitiveRender& primitives() {
		return mPrimitives;
	}
	// Cluster renderer for VirtualMeshRenderer components (settings, statistics, tests).
	VG::VirtualGeometryRenderer& virtualGeometry();
	void Init();
	void renderScene() override;
	void renderLoadingScene(SCENE_SYSTEM::Scene* scene);
	void drawDrawable(const Drawable& p_toDraw);

	void setPipeline(std::unique_ptr<RenderGraphPipeline>&& renderPipeline);
	std::shared_ptr<TextureInterface> getPipelineTexture(const std::string& name) const {
		return mRenderPipeline->mTextures.at(name);
	}
	void setPipelineAccelerationStructure(const std::string& name, std::shared_ptr<AccelerationStructure> structure) {
		if (name.empty() || !structure) {
			throw std::runtime_error("AS resource needs a name and nonnull structure");
		}
		mRenderPipeline->mAccelerationStructures[name] = std::move(structure);
	}
	void reloadPipelineSettings(const std::string& asset = {});
	void resetTemporalHistory(HistoryReason reason = HistoryReason::UserReset) {
		mRenderPipeline->mHistory.invalidate(reason, HistoryAllEffects);
	}
	// Cumulative history invalidation counters over every pipeline this renderer used.
	HistoryCounters historyCounters() const;
	// Prints GI_HISTORY_RESET / GI_HISTORY_KEPT style lines (see gameRenderer.cpp).
	void logHistoryCounters(bool force);
	void enableGpuProfiling(bool enabled = true);
	void resetGpuProfile();
	PROFILER::GpuProfileSnapshot gpuProfile() const;
	void printGpuProfile() const;

#ifdef OCULUS
	void renderSceneOculus(XrCompositionLayerProjectionView& layerView, render_target_t& rtarget, XrPosef& stagePose, uint32_t viewID);
#endif
};
}

namespace serde {
template <> struct serde_adaptor<nlohmann::json, IKIGAI::RENDER::PipelineStage::Descriptor::UniformTypeDescr> {
	using Variant = IKIGAI::RENDER::PipelineStage::Descriptor::UniformTypeDescr;
	static void from(nlohmann::json& json, std::string_view key, Variant& value) {
		using MaterialUniform = IKIGAI::RENDER::MaterialResource::UniformType;
		MaterialUniform uniform;
		serde_adaptor<nlohmann::json, MaterialUniform>::from(json, key, uniform);
		std::visit(
			[&](const auto& item) {
				if constexpr (std::is_assignable_v<Variant&, decltype(item)>) {
					value = item;
				}
				else {
					throw std::runtime_error("Pipeline uniform matrices are not supported");
				}
			},
			uniform);
	}
	static void into(nlohmann::json& json, std::string_view key, const Variant& value) {
		std::visit([&](const auto& uniform) { serialize_to<nlohmann::json>(uniform, json, key); }, value);
	}
};
}
