#pragma once

#include "mathModule/vector2.h"
#include <map>
#include <memory>
#include <string>

namespace IKIGAI::RENDER {
class TextureInterface;
}

namespace IKIGAI::GUI {
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

std::shared_ptr<Font> LoadLabelFont(const std::string& path);
}
