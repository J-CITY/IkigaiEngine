#include "modelRenderer.h"

#include "../object.h"
#include "generated/modelRenderer.generated.h"
#include "materialRenderer.h"
#include "resourceModule/modelManager.h"
#include "utilsModule/log/loggerDefine.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace IKIGAI;
using namespace IKIGAI::ECS;

ModelRenderer::ModelRenderer(UTILS::Ref<ECS::Object> p_owner)
	: ComponentBase(p_owner) {
	__NAME__ = "ModelRenderer";
}

ModelRenderer::ModelRenderer(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor)
	: ComponentBase(_obj) {
	__NAME__ = "ModelRenderer";
	setModelByPath(_descriptor.Path);
}

void ModelRenderer::setModel(std::shared_ptr<RENDER::ModelInterface> model) {
	this->m_model = model;
	obj->setModelEvent.run();
}

std::shared_ptr<RENDER::ModelInterface> ModelRenderer::getModel() const {
	return m_model;
}

void ModelRenderer::setFrustumBehaviour(EFrustumBehaviour mode) {
	m_frustumBehaviour = mode;
}

EFrustumBehaviour ModelRenderer::getFrustumBehaviour() const {
	return m_frustumBehaviour;
}

const RENDER::BoundingSphere& ModelRenderer::getCustomBoundingSphere() const {
	return m_customBoundingSphere;
}

void ModelRenderer::setCustomBoundingSphere(const RENDER::BoundingSphere& boundingSphere) {
	m_customBoundingSphere = boundingSphere;
}

void ModelRenderer::setModelByPath(std::string path) {
	m_model = path.empty() ? nullptr
						   : RESOURCES::ServiceManager::Get<RESOURCES::ModelLoader>().loadResource(
								 path, path.ends_with(".model") ? RESOURCES::ELoadingType::RESOURCE : RESOURCES::ELoadingType::FILE);
	setFrustumBehaviour(IKIGAI::ECS::EFrustumBehaviour::CULL_MODEL);
	auto bs = IKIGAI::RENDER::BoundingSphere();
	bs.position = { 0.0f, 0.0f, 0.0f };
	bs.radius = 1.0f;
	setCustomBoundingSphere(bs);
	// obj->setModelEvent.run();
	// TODO:: remove this dependence
	auto mat = obj->getComponent<MaterialRenderer>();
	if (!mat) {
		return;
	}
	mat->updateMaterialList();
}

std::string ModelRenderer::getModelPath() const {
	if (!m_model) {
		return "";
	}
	return m_model->getPath();
}

ModelRenderer::Descriptor ModelRenderer::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<ModelRenderer>();
	descriptor.Path = getModelPath();
	return descriptor;
}
//---------------------------------------

ModelLODRenderer::ModelLODRenderer(UTILS::Ref<ECS::Object> p_owner)
	: ComponentBase(p_owner) {
	__NAME__ = "ModelLODRenderer";
}

ModelLODRenderer::ModelLODRenderer(UTILS::Ref<ECS::Object> _obj, const Descriptor _descriptor)
	: ComponentBase(_obj) {
	__NAME__ = "ModelLODRenderer";
	setModelsByPath(_descriptor.Paths);
}

void ModelLODRenderer::setModel(const ModelLod& model) {
	auto models = m_models;
	models.push_back(model);
	setModels(models);
}

void ModelLODRenderer::setModels(const std::vector<ModelLod>& models) {
	auto sorted = models;
	for (const auto& model : sorted) {
		if (!model.m_model || !std::isfinite(model.m_distance) || model.m_distance < 0) {
			throw std::invalid_argument("LOD requires a model and a finite nonnegative distance");
		}
	}
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.m_distance < b.m_distance; });
	for (size_t index = 1; index < sorted.size(); ++index) {
		if (sorted[index - 1].m_distance == sorted[index].m_distance) {
			throw std::invalid_argument("LOD distances must be unique");
		}
	}
	m_models = std::move(sorted);
	obj->setModelEvent.run();
}

const std::vector<ModelLODRenderer::ModelLod>& ModelLODRenderer::getModels() const {
	return m_models;
}

std::shared_ptr<RENDER::ModelInterface> ModelLODRenderer::getModelByDistance(float distance) {
	if (m_models.empty() || !std::isfinite(distance)) {
		return nullptr;
	}
	for (const auto& model : m_models) {
		if (distance < model.m_distance) {
			return model.m_model;
		}
	}
	return m_models.back().m_model;
}

void ModelLODRenderer::setFrustumBehaviour(EFrustumBehaviour mode) {
	m_frustumBehaviour = mode;
}

EFrustumBehaviour ModelLODRenderer::getFrustumBehaviour() const {
	return m_frustumBehaviour;
}

const RENDER::BoundingSphere& ModelLODRenderer::getCustomBoundingSphere() const {
	return m_customBoundingSphere;
}

void ModelLODRenderer::setCustomBoundingSphere(const RENDER::BoundingSphere& boundingSphere) {
	m_customBoundingSphere = boundingSphere;
}

void ModelLODRenderer::setModelsByPath(std::vector<ModelLodRefl> paths) {
	std::vector<ModelLod> models;
	for (const auto& path : paths) {
		if (path.Path.empty()) {
			throw std::invalid_argument("LOD model path must not be empty");
		}
		auto model = RESOURCES::ServiceManager::Get<RESOURCES::ModelLoader>().loadResource(
			path.Path, path.Path.ends_with(".model") ? RESOURCES::ELoadingType::RESOURCE : RESOURCES::ELoadingType::FILE);
		models.push_back({ path.Distance, model });
	}
	setModels(models);
	if (auto material = obj->getComponent<MaterialRenderer>()) {
		material->updateMaterialList();
	}
}

std::vector<ModelLODRenderer::ModelLodRefl> ModelLODRenderer::getModelsPath() const {
	std::vector<ModelLODRenderer::ModelLodRefl> res;
	for (auto e : m_models) {
		res.push_back({ e.m_distance, e.m_model->getPath() });
	}
	return res;
}

ModelLODRenderer::Descriptor ModelLODRenderer::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<ModelLODRenderer>();
	descriptor.Paths = getModelsPath();
	return descriptor;
}

// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::ModelRenderer>("ModelRenderer")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Model", &IKIGAI::ECS::ModelRenderer::getModelPath, &IKIGAI::ECS::ModelRenderer::setModelByPath)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING_WITH_FILE_CHOOSE),
//		rttr::metadata(EditorMetaInfo::FILE_EXTENSION, std::string(".fbx,.obj"))
//	);
//
//
//	rttr::registration::class_<IKIGAI::ECS::ModelLODRenderer::ModelLodRefl>("ModelLodRefl")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Distance", &IKIGAI::ECS::ModelLODRenderer::ModelLodRefl::m_distance)
//	.property("Path", &IKIGAI::ECS::ModelLODRenderer::ModelLodRefl::m_path);
//
//
//	rttr::registration::class_<IKIGAI::ECS::ModelLODRenderer>("ModelLODRenderer")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Models", &IKIGAI::ECS::ModelLODRenderer::getModelsPath, &IKIGAI::ECS::ModelLODRenderer::setModelsByPath)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::MODEL_LOD),
//		rttr::metadata(EditorMetaInfo::FILE_EXTENSION, std::string(".fbx,.obj"))
//	);
// }

//----------------------------------------------

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ModelRenderer>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ModelLODRenderer>();
	return true;
}();
}
