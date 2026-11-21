#pragma once

#include "coreModule/gui/components/spriteComponent.h"
#include <memory>
#include <string>

namespace IKIGAI {
namespace GUI {
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
