#include "font.h"

#include "renderModule/backends/interface/resourceStruct.h"
#include "renderModule/render.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "utilsModule/serviceManager.h"
#include <algorithm>
#include <cstring>
#include <ft2build.h>
#include <stdexcept>
#include <vector>

#include FT_FREETYPE_H

using namespace IKIGAI;

GUI::Font::Font(std::string fontPath, int size)
	: path(fontPath) {
	FT_Library ft = nullptr;
	FT_Face face = nullptr;
	if (FT_Init_FreeType(&ft)) {
		throw std::runtime_error("Cannot initialize FreeType");
	}
	auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(fontPath);
	auto fontBytes = file && file->isValid() ? file->read() : std::vector<uint8_t> {};
	const auto fontError = fontBytes.empty() ? FT_New_Face(ft, fontPath.c_str(), 0, &face)
											 : FT_New_Memory_Face(ft, fontBytes.data(), static_cast<FT_Long>(fontBytes.size()), 0, &face);
	if (fontError) {
		FT_Done_FreeType(ft);
		throw std::runtime_error("Cannot open font: " + fontPath);
	}
	FT_Set_Pixel_Sizes(face, 0, std::max(size, 1));
	std::vector<uint8_t> pixels(1024 * 1024, 0);
	int x = 0, y = 0, rowHeight = 0;
	for (unsigned c = 0; c < 256; ++c) {
		if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
			continue;
		}
		const auto& bitmap = face->glyph->bitmap;
		if (x + bitmap.width > 1024) {
			x = 0;
			y += rowHeight + 1;
			rowHeight = 0;
		}
		if (y + bitmap.rows > 1024) {
			break;
		}
		Characters[static_cast<char>(c)] = { MATH::Vector2i(bitmap.width, bitmap.rows), MATH::Vector2i(x, y),
			MATH::Vector2i(face->glyph->bitmap_left, face->glyph->bitmap_top), static_cast<unsigned>(face->glyph->advance.x) };
		for (unsigned row = 0; row < bitmap.rows; ++row) {
			const auto source = bitmap.pitch >= 0 ? bitmap.buffer + row * bitmap.pitch : bitmap.buffer + (bitmap.rows - 1 - row) * (-bitmap.pitch);
			std::memcpy(pixels.data() + (y + row) * 1024 + x, source, bitmap.width);
		}
		x += bitmap.width + 1;
		rowHeight = std::max(rowHeight, static_cast<int>(bitmap.rows));
	}
	RENDER::TextureResource resource;
	resource.width = resource.height = 1024;
	resource.channels = 1;
	resource.pixelType = RENDER::PixelFormat::R_INT;
	resource.useMipmap = false;
	resource.colorData = std::move(pixels);
	texture = RESOURCES::ServiceManager::Get<RENDER::Renderer>().createTexture(resource);
	FT_Done_Face(face);
	FT_Done_FreeType(ft);
}

std::shared_ptr<GUI::Font> GUI::LoadLabelFont(const std::string& path) {
	const std::string requested = path.empty() ? "/fonts/a_AlternaSw.TTF" : path;
	auto& files = UTILS::ServiceManager::Get<RESOURCES::FileSystem>();
	const auto fontPath = files.getFilePath(requested);
	if (!fontPath) {
		throw std::runtime_error("Label font not found: " + requested);
	}
	return std::make_shared<Font>(*fontPath, 42);
}
