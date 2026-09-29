#pragma once
#include "drawable.h"
#include "frustum.h"
#include "gameRendererInterface.h"
#include "backends/interface/resourceStruct.h"

// TODO move include to .cpp
#ifdef OCULUS
#include "util_egl.h"
#include "util_oxr.h"
#endif

namespace IKIGAI::SCENE_SYSTEM
{
	class Scene;
}

namespace IKIGAI::ECS
{
	class CameraComponent;
}

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

	enum class DrawContent {
		FORWARD = 0,
		DEFERRED,
		GUI,
		QUAD
	};

	struct PipelineStage {
		using UniformType = std::variant<float, int, bool, MATH::Vector2f, MATH::Vector3f,
			MATH::Vector4f, std::shared_ptr<::IKIGAI::RENDER::TextureInterface>>;

		struct Descriptor {
			using UniformTypeDescr =
				std::variant<float, int, bool, MATH::Vector2f, MATH::Vector3f, MATH::Vector4f, std::string>;

			std::string Name;
			DrawContent Draw;
			std::string Material;
			std::string FrameBuffer;
			std::map<std::string, UniformTypeDescr> Uniforms;

			std::map<std::string, std::string> BufferLinks; // ShaderBufferName -> Pipeline/GlobalBufferName
			std::map<std::string, std::map<std::string, UniformTypeDescr>> BufferOverrides; // BufferName -> MemberName -> Value

			template <class Context>
			constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Name, "Name")
					.field(&Self::Draw, "Draw")
					.field(&Self::Material, "Material")
					.field(&Self::FrameBuffer, "FrameBuffer", default_{""})
					.field(&Self::Uniforms, "Uniforms")
					.field(&Self::BufferLinks, "BufferLinks")
					.field(&Self::BufferOverrides, "BufferOverrides");
			}
		};
		PipelineStage() = default;

		std::string mName;
		std::shared_ptr<::IKIGAI::RENDER::FrameBufferInterface> mFrameBuffer;
		std::shared_ptr<::IKIGAI::RENDER::MaterialInterface> mMaterial;
		DrawContent mDrawContent;
		std::map<std::string, UniformType> mUniforms;

		std::map<std::string, std::string> mBufferLinks;
		std::map<std::string, std::map<std::string, UniformType>> mBufferOverrides;
	};

	struct RenderGraphPipeline {
		struct TextureDesc {
			std::string Name;
			std::string Path;

			template <class Context>
			constexpr static auto serde(Context& context, TextureDesc& value) {
				using Self = TextureDesc;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Name, "Name")
					.field(&Self::Path, "Path");
			}
		};

		struct FrameDesc {
			std::string Name;
			std::vector<std::string> Textures;
			std::string Depth;

			template <class Context>
			constexpr static auto serde(Context& context, FrameDesc& value) {
				using Self = FrameDesc;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Name, "Name")
					.field(&Self::Textures, "Textures")
					.field(&Self::Depth, "Depth", default_{""});
			}
		};

		enum class BufferType { UNIFORM = 0, STORAGE };

		struct BufferDescriptor {
			std::string Name;
			size_t Size = 0;
			size_t Stride = 0; // Needed for SSBO
			BufferType Type; // UNIFORM or STORAGE

			template <class Context>
			constexpr static auto serde(Context& context, BufferDescriptor& value) {
				using Self = BufferDescriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Name, "Name")
					.field(&Self::Size, "Size", default_{0})
					.field(&Self::Stride, "Stride", default_{0})
					.field(&Self::Type, "Type", default_{BufferType::UNIFORM});
			}
		};

		struct Descriptor {
			std::string Name;
			std::vector<TextureDesc> Textures;
			std::vector<FrameDesc> FrameBuffers;
			std::vector<BufferDescriptor> Buffers;
			std::vector<PipelineStage::Descriptor> StartStages;
			std::vector<PipelineStage::Descriptor> Stages;

			template <class Context>
			constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Name, "Name")
					.field(&Self::Textures, "Textures")
					.field(&Self::FrameBuffers, "FrameBuffers")
					.field(&Self::Buffers, "Buffers")
					.field(&Self::Stages, "Stages")
					.field(&Self::StartStages, "StartStages");
			}
		};

		RenderGraphPipeline() = default;
		RenderGraphPipeline(const Descriptor& descriptor, RENDER::Renderer& renderer);

		std::map<std::string, std::shared_ptr<RENDER::FrameBufferInterface>> mFrameBuffers;
		std::map<std::string, std::shared_ptr<RENDER::TextureInterface>> mTextures;
		std::map<std::string, std::shared_ptr<RENDER::StorageBufferInterface>> mStorageBuffers;
		std::map<std::string, std::shared_ptr<RENDER::UniformBufferInterface>> mUniformBuffers;

		using StagesArr = std::list<std::unique_ptr<PipelineStage>>;

		StagesArr mStartStages;
		StagesArr mStages;

		void run();

	//private:
		bool mIsInitialized = false;

		StagesArr loadStages(const std::vector<PipelineStage::Descriptor>& stages);
	};

	class GameRenderer : public RENDER::GameRendererInterface {
		friend struct RenderGraphPipeline;

		IKIGAI::CORE::Core& mContext;
		std::unique_ptr<RenderGraphPipeline> mRenderPipeline;

		std::shared_ptr<StorageBufferInterface> mLightSSBO;
		std::shared_ptr<RENDER::MaterialInterface> mEmptyMaterial;
		std::shared_ptr<RENDER::TextureInterface> mEmptyTexture;
		std::shared_ptr<UniformBufferInterface> mEngineUbo;
		std::shared_ptr<UniformBufferInterface> mBoneUbo;

		EngineUBO uboData;

		unsigned long long mFrameCount = 0ull;

		void renderScene(IKIGAI::SCENE_SYSTEM::Scene& scene, IKIGAI::ECS::CameraComponent&);
		void updateLights(SCENE_SYSTEM::Scene& scene);
		void updateLightsInFrustum(SCENE_SYSTEM::Scene& scene, const Frustum& frustum);

		const RenderGraphPipeline& getCurrentPipeline() const override;

	public:
		GameRenderer(CORE::Core& context);
		void Init();
		void renderScene() override;
		void drawDrawable(const Drawable& p_toDraw);

		void setPipeline(std::unique_ptr<RenderGraphPipeline>&& renderPipeline);

#ifdef OCULUS
		void renderSceneOculus(XrCompositionLayerProjectionView& layerView,
			render_target_t& rtarget, XrPosef& stagePose,
			uint32_t viewID);
#endif
	};
}