#pragma once
#include "mathModule/vector2.h"
#include <ft2build.h>
#include <memory>
#include <string>

#include "components/spriteComponent.h"
#include <sceneModule/sceneManager.h>

#include FT_FREETYPE_H

namespace IKIGAI {
namespace RENDER {
	class MaterialInterface;
}

namespace RESOURCES {
	class Shader;
}

namespace CORE_SYSTEM {
	class Core;
}
namespace GUI {
	class ComponentGui;

	struct Character {
		MATH::Vector2i Size;
		MATH::Vector2i Start;
		MATH::Vector2i Bearing;
		unsigned int Advance;
	};

	struct Font {
		std::string path;
		std::map<char, Character> Characters;
		std::shared_ptr<RENDER::TextureInterface> texture;

		Font(std::string fontPath, int size);
	};
	struct GuiHelper {
		static std::shared_ptr<ECS::Object> CreateSprite(const std::string& name, const std::string& path, bool isRoot = false);
		static std::shared_ptr<ECS::Object> CreateLabel(const std::string& name, const std::string& label, bool isRoot = false);
		static std::shared_ptr<ECS::Object> CreateScroll(const std::string& name, int w, int h, bool isRoot = false);
		static std::shared_ptr<ECS::Object> CreateClip(const std::string& name, int w, int h, bool isRoot = false);
		static std::shared_ptr<ECS::Object> CreateLayout(const std::string& name, IKIGAI::ECS::LayoutComponent::Type type, bool isRoot = false);
		static std::shared_ptr<ECS::Object> CreateButton(const std::string& name, bool isRoot = false);
		static std::shared_ptr<ECS::Object> CreateSdfLabel(
			const std::string& name, const std::string& text, const std::string& font = "/fonts/default-sdf.font");
		static std::shared_ptr<ECS::Object> CreateVectorShape(const std::string& name, MATH::Vector2f size, int shape = 0);
		static std::shared_ptr<ECS::Object> CreateVectorPath(const std::string& name, const std::string& commands);
		static std::shared_ptr<ECS::Object> CreateVectorCanvas(const std::string& name, bool worldSpace = false);
	};
}
}
