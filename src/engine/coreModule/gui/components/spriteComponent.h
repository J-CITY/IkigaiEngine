#pragma once

#include "../../ecs/components/component.h"
#include <renderModule/spine/spineDrawable.h>

#include "mathModule/math.h"
#include "particleModule/particles.h"
#include "renderModule/backends/interface/textureInterface.h"

namespace IKIGAI {
namespace RESOURCES {
	class Shader;
	struct ParticleAsset;
}
}

namespace IKIGAI {
class Vertex;

namespace RENDER {
	class VertexArray;
	struct AtlasRect;
	class ShaderInterface;
	class VertexBufferInterface;
	class TextureInterface;
	class MaterialInterface;
	// class Material;
}

namespace GUI {
	class Font;
}
}

namespace IKIGAI {
namespace ECS {
	class Object;

	struct BatchVertex {
		MATH::Vector3f position;
		MATH::Vector2f texCoord;
		MATH::Vector4f color;
	};

	class SpriteBatcher {

	public:
		SpriteBatcher();
		~SpriteBatcher();

		void Draw(const std::array<MATH::Vector4f, 6>& verts, const RENDER::AtlasRect& uv, MATH::Vector4f color,
			std::shared_ptr<RENDER::TextureInterface> texture, std::shared_ptr<RENDER::ShaderInterface> shader, bool is3D = false);
		void Draw(const std::array<Vertex, 6>& verts, std::shared_ptr<RENDER::TextureInterface> texture,
			std::shared_ptr<RENDER::ShaderInterface> shader, bool is3D = false);
		void Flush();

	private:
		// Collection of vertices
		std::vector<Vertex> mVertexBuffer;

		std::shared_ptr<RENDER::VertexBufferInterface> mVbo;
		std::shared_ptr<RENDER::VertexArray> mVertexArray;

		// unsigned m_vbo = 0;

		// Shaders.
		// Shader m_vertexShader;
		// Shader m_fragmentShader;

		// GL index for the shader program
		// unsigned m_shaderProgram = 0;

		// Texture and uniform location
		std::shared_ptr<RENDER::TextureInterface> mTexture;
		std::shared_ptr<RENDER::ShaderInterface> mShader;
		bool mIs3D = false;
		unsigned m_textureUniform = 0;

		// stuff to send word matrices
		// MATHGL::Matrix4 m_screenTransform;
		// unsigned m_screenTransformUniform = 0;
	};

	class RootGuiComponent : public ComponentBase {
	public:
		RootGuiComponent(Object& obj)
			: ComponentBase(obj) { }

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value).field(&Self::Type, "RootGuiComponentType");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		RootGuiComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		RootGuiComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		static auto GetMembers() {
			return std::tuple { };
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<RootGuiComponent>() {
		return "class IKIGAI::ECS::RootGuiComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<RootGuiComponent>() {
		return "RootGuiComponent";
	}

	class ColorComponent : public ComponentBase {
	public:
		ColorComponent(Object& obj)
			: ComponentBase(obj) { }

		MATH::Vector4f mColor = { 1, 1, 1, 1 };

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			MATH::Vector4f Color;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value).field(&Self::Type, "ColorComponentType").field(&Self::Color, "Color");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;
		ColorComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		ColorComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple { IKIGAI::UTILS::MakeMemberInfo("Color", &ColorComponent::mColor,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f { 0.0f, 10000.0f } },
				{ UTILS::MetaParam::EDIT_STEP, 0.1f },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT_4 },
				}) };
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<ColorComponent>() {
		return "class IKIGAI::ECS::ColorComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<ColorComponent>() {
		return "ColorComponent";
	}

	class SpriteComponent : public ComponentBase {
	public:
		SpriteComponent(Object& obj);
		SpriteComponent(Object& obj, const std::string& path);
		void setTexture(std::string path);
		void setTextureAtlas(std::string path);
		std::string getTexture();

		void setAtlasPiece(std::string name);

		std::string getAtlasPiece();

		MATH::Vector4f mColor = { 1, 1, 1, 1 };
		std::string mPath;
		std::shared_ptr<RENDER::TextureInterface> mTexture;
		std::string mTexturePiece;
		bool mExplicitAtlas = false;

		// move to component
		bool mIs3D = false;

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			// MATH::Vector4f Color;
			std::string TexturePath;
			std::string TextureAtlasPath;
			std::string TexturePiece;
			bool Is3D = false;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "SpriteComponentType")
					.field(&Self::TexturePath, "TexturePath", default_ { "" })
					.field(&Self::TexturePiece, "TexturePiece", default_ { "" })
					.field(&Self::TextureAtlasPath, "TextureAtlasPath", default_ { "" })
					.field(&Self::Is3D, "Is3D", default_ { false });
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		SpriteComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		SpriteComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple { IKIGAI::UTILS::MakeMemberInfo("Is3D", &SpriteComponent::mIs3D,
									UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("TexturePath", &SpriteComponent::mPath,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("TexturePiece", &SpriteComponent::mTexturePiece,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }) };
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<SpriteComponent>() {
		return "class IKIGAI::ECS::SpriteComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<SpriteComponent>() {
		return "SpriteComponent";
	}

	class SpriteAnimateComponent : public SpriteComponent {
	public:
		SpriteAnimateComponent(Object& obj)
			: SpriteComponent(obj) { };
		SpriteAnimateComponent(Object& obj, const std::string& path)
			: SpriteComponent(obj, path) { };

		void setGridSize(MATH::Vector2f sz);

		MATH::Vector2f getGridSize();

		void setFrameCount(int e);

		int getFrameCount() const;

		// TODO: move to system
		void updateAnim();
		void nextFrame();

		int mFrameCount = 0;
		int mCurrentFreme = 0;
		MATH::Vector2f mGridSize;

		float mTime = 1000 / 25.0f;
		float mCutTime = mTime;

		float mTimeScale = 200.0f;
		RENDER::AtlasRect mUVRect;

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			// MATH::Vector4f Color;
			std::string TexturePath;
			std::string TexturePiece;
			bool Is3D = false;
			MATH::Vector2f GridSize;
			int FrameCount;
			float TimeScale = 1.0f;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "SpriteAnimateComponentType")
					.field(&Self::TexturePath, "TexturePath", default_ { "" })
					.field(&Self::TexturePiece, "TexturePiece", default_ { "" })
					.field(&Self::Is3D, "Is3D", default_ { false })
					.field(&Self::GridSize, "GridSize")
					.field(&Self::FrameCount, "FrameCount")
					.field(&Self::TimeScale, "TimeScale", default_ { 1.0f });
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		SpriteAnimateComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		SpriteAnimateComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple { // IKIGAI::UTILS::MakeMemberInfo("Is3D", &SpriteAnimateComponent::mIs3D,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				//}),
				// IKIGAI::UTILS::MakeMemberInfo("TexturePath", &SpriteAnimateComponent::mPath,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				//}),
				// IKIGAI::UTILS::MakeMemberInfo("TexturePiece", &SpriteAnimateComponent::mTexturePiece,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				//}),
				IKIGAI::UTILS::MakeMemberInfo("GridSize", &SpriteAnimateComponent::mGridSize,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("TimeScale", &SpriteAnimateComponent::mTimeScale,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } })
			};
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<SpriteAnimateComponent>() {
		return "class IKIGAI::ECS::SpriteAnimateComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<SpriteAnimateComponent>() {
		return "SpriteAnimateComponent";
	}

	class SpriteParticleComponent : public SpriteComponent {
	public:
		~SpriteParticleComponent() override;
		SpriteParticleComponent(const SpriteParticleComponent&);
		SpriteParticleComponent& operator=(const SpriteParticleComponent&);
		void tick(double dt);
		const auto* runtime() const {
			return mRuntime.get();
		}
		const auto& asset() const {
			return mParticleAsset;
		}
		SpriteParticleComponent(Object& obj);
		;
		SpriteParticleComponent(Object& obj, const std::string& path);
		;

		struct Particle {
			MATH::Vector3f pos;
			MATH::Vector3f speed;
			float life = 0.0f;
			float size = 0.0f, weight = 0.0f;

			int emmiterId = 0;
			bool isAlive = false;
		};

		struct Emmiter {
			int count = 0;

			MATH::Vector3f localPos;
			MATH::Vector3f gravity;
			MATH::Vector4f color;

			MATH::Vector2f size;
			MATH::Vector2f weight;
			MATH::Vector2f angle;
			MATH::Vector2f angleForce;
			MATH::Vector2f lifeTime;
			// TODO: param for change alpha

			std::string piece;

			float pause = 0.0f;
			float spawnTime = 0.0f;
			int spawnCount = 0;
			float curSpawnTime = 0.0f;
			int curParticleCount = 0;

			template <class Context> constexpr static auto serde(Context& context, Emmiter& value) {
				using Self = Emmiter;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::count, "Count")
					.field(&Self::spawnTime, "SpawnTime")
					.field(&Self::spawnCount, "SpawnCount")
					.field(&Self::pause, "Pause")
					.field(&Self::localPos, "LocalPos")
					.field(&Self::gravity, "Gravity")
					.field(&Self::color, "Color")
					.field(&Self::size, "Scale")
					.field(&Self::weight, "Weight")
					.field(&Self::angle, "Angle")
					.field(&Self::angleForce, "AngleForce")
					.field(&Self::lifeTime, "LifeTime")
					.field(&Self::piece, "Piece", default_ { "" });
			}
		};

		void setEmmiters(std::vector<Emmiter> e);

		std::vector<Emmiter> getEmmiters();

		void spawnParticle(Emmiter& emmiter, int i);
		// TODO: move to system
		void update();

		// TODO: add play/pause, add support animation, add change alpha by life time

		std::vector<Particle> particles;
		std::vector<int> freeParticlesId;

		std::vector<Emmiter> emmiters;
		std::shared_ptr<RESOURCES::ParticleAsset> mParticleAsset;
		std::unique_ptr<PARTICLES::Runtime<2>> mRuntime;

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			std::string TexturePath;
			std::string TextureAtlasPath;
			std::string TexturePiece;
			std::vector<Emmiter> Emmiters;
			bool Is3D = false;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "SpriteParticleComponentType")
					.field(&Self::TexturePath, "TexturePath", default_ { "" })
					.field(&Self::TextureAtlasPath, "TextureAtlasPath", default_ { "" })
					.field(&Self::TexturePiece, "TexturePiece", default_ { "" })
					.field(&Self::Is3D, "Is3D", default_ { false })
					.field(&Self::Emmiters, "Emmiters");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		SpriteParticleComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		SpriteParticleComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple {
				// IKIGAI::UTILS::MakeMemberInfo("Is3D", &SpriteParticleComponent::mIs3D,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				//}),
				// IKIGAI::UTILS::MakeMemberInfo("TexturePath", &SpriteParticleComponent::mPath,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				//}),
				// IKIGAI::UTILS::MakeMemberInfo("TexturePiece", &SpriteParticleComponent::mTexturePiece,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				//})
			};
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<SpriteParticleComponent>() {
		return "class IKIGAI::ECS::SpriteParticleComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<SpriteParticleComponent>() {
		return "SpriteParticleComponent";
	}

	class LabelComponent : public ComponentBase {
	public:
		LabelComponent(Object& obj);
		LabelComponent(Object& obj, std::string label, std::shared_ptr<GUI::Font> font);
		std::string mLabel;
		std::shared_ptr<GUI::Font> mFont;
		MATH::Vector4f color = { 1, 1, 1, 1 };

		// move to component
		bool mIs3D = false;

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			std::string Label;
			std::string Font;
			bool Is3D = false;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "LabelComponentType")
					.field(&Self::Label, "Label")
					.field(&Self::Font, "Font", default_ { "" })
					.field(&Self::Is3D, "Is3D", default_ { false });
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		LabelComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		LabelComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		static auto GetMembers() {
			return std::tuple {
				IKIGAI::UTILS::MakeMemberInfo("Is3D", &LabelComponent::mIs3D,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("Label", &LabelComponent::mLabel,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				// IKIGAI::UTILS::MakeMemberInfo("Font", &SpriteAnimateComponent::mFont,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				// })
			};
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<LabelComponent>() {
		return "class IKIGAI::ECS::LabelComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<LabelComponent>() {
		return "LabelComponent";
	}

	struct SpineRefl {
		std::string skelPath;
		std::string atlasPath;
	};
	class SpineComponent : public ComponentBase {

		void setSpine(SpineRefl data);

		SpineRefl getSpine();

	public:
		SpineComponent(Object& obj);
		SpineComponent(Object& obj, std::string skelPath, std::string atlasPath);
		std::shared_ptr<RENDER::SPINE::SpineController> spine;

		std::string skelPath;
		std::string atlasPath;

		// move to component TODO:add support 3d in shader
		bool mIs3D = false;

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			std::string SkelPath;
			std::string AtlasPath;
			bool Is3D = false;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "SpineComponentType")
					.field(&Self::SkelPath, "SkelPath")
					.field(&Self::AtlasPath, "AtlasPath")
					.field(&Self::Is3D, "Is3D");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		SpineComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		SpineComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple {
				IKIGAI::UTILS::MakeMemberInfo("Is3D", &SpineComponent::mIs3D,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("SkelPath", &SpineComponent::skelPath,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("AtlasPath", &SpineComponent::atlasPath,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				// IKIGAI::UTILS::MakeMemberInfo("Font", &SpriteAnimateComponent::mFont,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				// })
			};
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<SpineComponent>() {
		return "class IKIGAI::ECS::SpineComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<SpineComponent>() {
		return "SpineComponent";
	}

	enum class GuiEventType { NONE, COVER, PRESS, PRESS_CONTINUE, RELEASE, UNCOVER };
	class InteractionComponent : public ComponentBase {
	public:
		GuiEventType mCurEvent = GuiEventType::NONE;

		std::function<void()> mOnPress;
		std::function<void()> mOnPressContinue;
		std::function<void()> mOnRelease;
		std::function<void()> mOnCover;
		std::function<void()> mOnUncover;

		float mGlobalX = 0.0f;
		float mGlobalY = 0.0f;

		float mWidth = 0.0f;
		float mHeight = 0.0f;
		InteractionComponent(Object& obj);
		InteractionComponent(Object& obj, float w, float h);

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			float Height;
			float Width;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "InteractionComponentType")
					.field(&Self::Width, "Width")
					.field(&Self::Height, "Height");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		InteractionComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		InteractionComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple {
				IKIGAI::UTILS::MakeMemberInfo("Width", &InteractionComponent::mWidth,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("Height", &InteractionComponent::mHeight,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				// IKIGAI::UTILS::MakeMemberInfo("Font", &SpriteAnimateComponent::mFont,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				// })
			};
		}

		// private:
		bool contains(float x, float y);
	};

	template <> inline std::string IKIGAI::ECS::GetType<InteractionComponent>() {
		return "class IKIGAI::ECS::InteractionComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<InteractionComponent>() {
		return "InteractionComponent";
	}

	class ClipComponent : public ComponentBase {
	public:
		float mGlobalX = 0.0f;
		float mGlobalY = 0.0f;

		float mWidth = 0.0f;
		float mHeight = 0.0f;
		ClipComponent(Object& obj);
		ClipComponent(Object& obj, float w, float h);

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			float Height;
			float Width;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "ClipComponentType")
					.field(&Self::Width, "Width")
					.field(&Self::Height, "Height");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		ClipComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		ClipComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple {
				IKIGAI::UTILS::MakeMemberInfo("Width", &ClipComponent::mWidth,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("Height", &ClipComponent::mHeight,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				// IKIGAI::UTILS::MakeMemberInfo("Font", &SpriteAnimateComponent::mFont,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				// })
			};
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<ClipComponent>() {
		return "class IKIGAI::ECS::ClipComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<ClipComponent>() {
		return "ClipComponent";
	}

	class ScrollComponent : public ComponentBase {
	public:
		bool mIsScrollHorizontal = true;
		bool mIsScrollVertical = false;
		float mWidth = 0.0f;
		float mHeight = 0.0f;

		float mStartX = 0.0f;
		float mStartY = 0.0f;
		bool mIsPress = false;

		std::shared_ptr<Object> mSelectedObj;
		MATH::Vector2f mSelectedObjPos;

		ScrollComponent(Object& obj);
		ScrollComponent(Object& obj, float w, float h);

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			float Height;
			float Width;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "ScrollComponentType")
					.field(&Self::Width, "Width")
					.field(&Self::Height, "Height");
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		ScrollComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		ScrollComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple {
				IKIGAI::UTILS::MakeMemberInfo("Width", &ScrollComponent::mWidth,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				IKIGAI::UTILS::MakeMemberInfo("Height", &ScrollComponent::mHeight,
					UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }),
				// IKIGAI::UTILS::MakeMemberInfo("Font", &SpriteAnimateComponent::mFont,
				// UTILS::Meta_t{
				//	{UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR}
				// })
			};
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<ScrollComponent>() {
		return "class IKIGAI::ECS::ScrollComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<ScrollComponent>() {
		return "ScrollComponent";
	}

	class LayoutComponent : public ComponentBase {
	public:
		enum class Type { HORIZONTAL, VERTICAL };
		// Cross-axis alignment of children inside the layout's content box.
		enum class Alignment { START, CENTER, END };
		Type mType = Type::HORIZONTAL;
		Alignment mAlignment = Alignment::START;

		// Gap between consecutive children on the main axis (horizontal layout -> horizontal offset, vertical -> vertical offset).
		// The other offset shifts the whole stack on the cross axis.
		float mHorizontalOffset = 0.0f;
		float mVerticalOffset = 0.0f;

		// Solver outputs / state. Not serialized.
		MATH::Vector2f mContentSize { 0.0f, 0.0f };
		bool mDirty = true;
		std::size_t mLastSignature = 0;

		// Forces the solver to re-run on the next GuiLayoutSystem update (e.g. after changing offsets or child sizes).
		void markDirty() {
			mDirty = true;
		}

		LayoutComponent(Object& obj)
			: ComponentBase(obj) { }
		LayoutComponent(Object& obj, Type type)
			: ComponentBase(obj)
			, mType(type) { }

		struct Descriptor : public ComponentBase::Descriptor {
			std::string Type;
			LayoutComponent::Type Layout;
			LayoutComponent::Alignment Alignment = LayoutComponent::Alignment::START;
			float HorizontalOffset = 0.0f;
			float VerticalOffset = 0.0f;
			template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
				using Self = Descriptor;
				using namespace serde::attribute;
				serde::serde_struct(context, value)
					.field(&Self::Type, "LayoutComponentType")
					.field(&Self::Layout, "Layout")
					.field(&Self::Alignment, "Alignment", default_ { LayoutComponent::Alignment::START })
					.field(&Self::HorizontalOffset, "HorizontalOffset", default_ { 0.0f })
					.field(&Self::VerticalOffset, "VerticalOffset", default_ { 0.0f });
			}
		};
		[[nodiscard]] Descriptor getDescriptor() const;

		LayoutComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);

		LayoutComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor);
		;
		static auto GetMembers() {
			return std::tuple { IKIGAI::UTILS::MakeMemberInfo("Layout", &LayoutComponent::mType,
				UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR } }) };
		}
	};

	template <> inline std::string IKIGAI::ECS::GetType<LayoutComponent>() {
		return "class IKIGAI::ECS::LayoutComponent";
	}

	template <> inline std::string IKIGAI::ECS::GetComponentName<LayoutComponent>() {
		return "LayoutComponent";
	}
}
}
