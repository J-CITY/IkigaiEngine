#include "materialRenderer.h"

#include "modelRenderer.h"
#include "transform.h"
#include "coreModule/ecs/object.h"
#include "resourceModule/materialManager.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

MaterialRenderer::MaterialRenderer(UTILS::Ref<ECS::Object> _obj) : ComponentBase(_obj) {
	__NAME__ = "MaterialRenderer";
	materials.fill(nullptr);
}

MaterialRenderer::MaterialRenderer(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor) : ComponentBase(_obj) {
	__NAME__ = "MaterialRenderer";
	setMaterialsNames(_descriptor.MaterialNames);
	setMaterialsByPath(_descriptor.Materials);
}

MaterialRenderer::~MaterialRenderer() {
	//this->obj->setModelEvent.removeListener(*setMaterialEventId);
}

void MaterialRenderer::updateMaterialList() {
	//TODO:: remove this dependence
	auto model = obj->getComponent<ModelRenderer>();
	if (!model || !model->getModel()) {
		return;
	}
	const auto& matNames = model->getModel()->getMaterialsNames();

	if (matNames.size() > 255) {
		return;
	}

	for (auto i = 0u; i < matNames.size(); i++) {
		//materials[i] = RESOURCES::MaterialLoader::Create("Materials/simple.mat");
		if (!materials[i]) {
			materials[i] = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource("Materials/simple.mat");
		}
		materialNames[i] = matNames[i];
	}
}

void MaterialRenderer::fillWithMaterial(std::shared_ptr<RENDER::MaterialInterface> material) {
	for (auto i = 0u; i < materials.size(); i++) {
		materials[i] = material;
	}
}

void MaterialRenderer::setMaterial(unsigned index, std::shared_ptr<RENDER::MaterialInterface> p_material) {
	materials[index] = p_material;
}

std::shared_ptr<RENDER::MaterialInterface> MaterialRenderer::GetMaterialAtIndex(unsigned index) {
	return materials.at(index);
}

void MaterialRenderer::removeMaterial(unsigned index) {
	if (index < materials.size()) {
		materials[index] = nullptr;;
	}
}

void MaterialRenderer::removeMaterial(std::shared_ptr<RENDER::MaterialInterface> p_instance) {
	for (uint8_t i = 0; i < materials.size(); i++) {
		if (materials[i] == p_instance) {
			materials[i] = nullptr;
		}
	}
}

void MaterialRenderer::removeMaterials() {
	for (auto i = 0u; i < materials.size(); i++) {
		materials[i] = nullptr;
	}
}

const MaterialRenderer::MaterialList& MaterialRenderer::getMaterials() const {
	return materials;
}

const MaterialRenderer::MaterialNames& MaterialRenderer::getMaterialNames() {
	return materialNames;
}

void MaterialRenderer::setMaterialsByPath(std::vector<std::string> paths) {
	int i = 0;
	for (auto& m : paths) {
		materials[i] = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource(m);
		i++;
	}
}

std::vector<std::string> MaterialRenderer::getMaterialsPaths() const {
	std::vector<std::string> res;
	for (auto& e : materials) {
		if (!e) {
			break;
		}
		res.push_back(e->getPath());
	}
	return res;
}

void MaterialRenderer::setMaterialsNames(std::vector<std::string> paths) {
	int i = 0;
	for (const auto& m : paths) {
		materialNames[i] = m;
		i++;
	}
}

std::vector<std::string> MaterialRenderer::getMaterialsNames() const {
	std::vector<std::string> res;
	int i = 0;
	for (const auto& e : materialNames) {
		if (!materials[i]) {
			break;
		}
		res.push_back(e);
		++i;
	}
	return res;
}

MaterialRenderer* MaterialRenderer::getMaterialRenderer() {
	return obj->getComponent<MaterialRenderer>().get();
}

MaterialRenderer::Descriptor MaterialRenderer::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<MaterialRenderer>();
	descriptor.MaterialNames = getMaterialsNames();
	descriptor.Materials = getMaterialsPaths();
	return descriptor;
}
//#include <rttr/registration>
//
//RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::MaterialRenderer>("MaterialRenderer")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Materials", &IKIGAI::ECS::MaterialRenderer::getMaterialsPaths, &IKIGAI::ECS::MaterialRenderer::setMaterialsByPath)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRINGS_ARRAY)
//	)
//	.property("MaterialsNames", &IKIGAI::ECS::MaterialRenderer::getMaterialsNames, &IKIGAI::ECS::MaterialRenderer::setMaterialsNames)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRINGS_ARRAY)
//	)
//	.property_readonly("Material", &IKIGAI::ECS::MaterialRenderer::getMaterialRenderer)//TODO: think about
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::MATERIAL)
//	)
//	;
//}