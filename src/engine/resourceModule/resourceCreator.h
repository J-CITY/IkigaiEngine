#pragma once
#include "resourceManager.h"
#include "renderModule/backends/interface/resourceStruct.h"


namespace IKIGAI
{
	namespace RENDER
	{
		class TextureInterface;
	}
}

namespace IKIGAI::RESOURCES {
	class TextureResourceCreatorInterface {
	public:
		TextureResourceCreatorInterface() = default;
		virtual ~TextureResourceCreatorInterface() = default;
		virtual ResourcePtr<RENDER::TextureInterface> createFromFile(const std::string& filepath, bool generateMipmap) = 0;
		virtual ResourcePtr<RENDER::TextureInterface> createFromMemory(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap) = 0;
		virtual ResourcePtr<RENDER::TextureInterface> createFromResource(const RENDER::TextureResource& res) = 0;
	};

	class ResourceCreator : public TextureResourceCreatorInterface {
	public:
		ResourcePtr<RENDER::TextureInterface> createFromFile(const std::string& filepath, bool generateMipmap) override;
		ResourcePtr<RENDER::TextureInterface> createFromMemory(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap) override;
		ResourcePtr<RENDER::TextureInterface> createFromResource(const RENDER::TextureResource& res) override;
	};
}
