#include "resourceCreator.h"
#include "renderModule/backends/interface/driverInterface.h"

IKIGAI::RESOURCES::ResourcePtr<IKIGAI::RENDER::TextureInterface> IKIGAI::RESOURCES::ResourceCreator::createFromFile(const std::string& filepath, bool generateMipmap)
{
	return RENDER::DriverInterface::Get()->createTexture(filepath, generateMipmap);
}

IKIGAI::RESOURCES::ResourcePtr<IKIGAI::RENDER::TextureInterface> IKIGAI::RESOURCES::ResourceCreator::createFromMemory(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap) {
	return RENDER::DriverInterface::Get()->createTexture(name, data, generateMipmap);
}

IKIGAI::RESOURCES::ResourcePtr<IKIGAI::RENDER::TextureInterface> IKIGAI::RESOURCES::ResourceCreator::createFromResource(const RENDER::TextureResource& res) {
	return RENDER::DriverInterface::Get()->createTexture(res);
}
