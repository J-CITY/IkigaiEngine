#pragma once
#include <cstdint>
#include <map>
#include <vector>

#ifdef OPENGL_BACKEND
#include <memory>
#include <string>

#include "../interface/textureInterface.h"
#include "../interface/atlasInterface.h"
#include "../interface/driverInterface.h"
#include <utilsModule/memoryAlloc.h>
#include <serdepp/serde.hpp>

namespace IKIGAI::RENDER {
	struct TextureResource;

	class TextureGl : public TextureInterface {
	protected:
		void create(const TextureResource& descriptor, const std::vector<void*>& data);

	public:
		TextureGl() = default;
		TextureGl(const TextureResource& descriptor, const std::vector<void*>& data);
		~TextureGl() override;

		void generateMips();

		void* getImguiId() override;
		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		unsigned id = 0;
		int slot = 0;

		static std::shared_ptr<TextureGl> Create(const TextureResource& descriptor, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureGl> Create(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureGl> Create(const std::string& path, bool getMipMap = true, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureGl> CreateFromMemory(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);

		void bind(int slot);
		void unbind();
		void bindImage(uint32_t unit, uint32_t mip_level, uint32_t layer, unsigned access, unsigned format);
	};

	class TextureAtlasGl : public TextureGl {
	private:
		AtlasData mAtlas;
	public:
		TextureAtlasGl() : TextureGl() {}
		TextureAtlasGl(const TextureResource& descriptor, const std::vector<void*>& data);

		void recreate(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData) override;

		[[nodiscard]] AtlasRect getPiece(const std::string& name) const;
		[[nodiscard]] AtlasRect getPieceUV(const std::string& name) const;

		static std::shared_ptr<TextureAtlasGl> CreateAtlas(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasGl> CreateAtlasFromResource(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
		static std::shared_ptr<TextureAtlasGl> CreateAtlasFromResource(const TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr);
	};
}
#endif
