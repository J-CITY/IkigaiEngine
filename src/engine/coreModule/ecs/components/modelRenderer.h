#pragma once
#include "chunkModelRenderer.h"
#include "component.h"
#include <renderModule/backends/interface/modelInterface.h>
#include <utilsModule/event.h>

#include "utilsModule/meshGenerator.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
enum class EFrustumBehaviour { DISABLED = 0, CULL_MODEL = 1, CULL_MESHES = 2, CULL_CUSTOM = 3 };
IKI_CLASS(Groups = [Component])
class ModelRenderer : public ComponentBase {
	IKI_GENERATED_BODY(ModelRenderer)
public:
	IKI_CLASS(Name = ModelRenderer::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(ModelRenderer::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "ModelRendererType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "Path"))
		std::string Path;
	};
	ModelRenderer(UTILS::Ref<ECS::Object> obj);
	ModelRenderer(UTILS::Ref<ECS::Object> obj, const Descriptor& _descriptor);
	ModelRenderer(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: ModelRenderer(obj, static_cast<const Descriptor&>(descriptor)) { };
	void setModel(std::shared_ptr<RENDER::ModelInterface> model);
	std::shared_ptr<RENDER::ModelInterface> getModel() const;
	void setFrustumBehaviour(EFrustumBehaviour boundingMode);
	EFrustumBehaviour getFrustumBehaviour() const;
	const RENDER::BoundingSphere& getCustomBoundingSphere() const;
	void setCustomBoundingSphere(const RENDER::BoundingSphere& boundingSphere);
	[[nodiscard]] Descriptor getDescriptor() const;

private:
	void setModelByPath(std::string path);
	std::string getModelPath() const;

	std::shared_ptr<RENDER::ModelInterface> m_model = nullptr;
	RENDER::BoundingSphere m_customBoundingSphere = { {}, 1.0f };
	EFrustumBehaviour m_frustumBehaviour = EFrustumBehaviour::CULL_MODEL;

public:
	static auto GetMembers() {
		return std::tuple {};
	}
};

IKI_CLASS(Groups = [Component])
class ModelLODRenderer : public ComponentBase {
	IKI_GENERATED_BODY(ModelLODRenderer)
public:
	IKI_CLASS(Name = ModelLODRenderer::ModelLodRefl)
	struct ModelLodRefl {
		IKI_GENERATED_BODY(ModelLODRenderer::ModelLodRefl)
		IKI_PROPERTY(SEREALIZE(name = "Distance"))
		float Distance = 0.0f;
		IKI_PROPERTY(SEREALIZE(name = "Path"))
		std::string Path;
	};
	IKI_CLASS(Name = ModelLODRenderer::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(ModelLODRenderer::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "ModelLODRendererType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "Paths"))
		std::vector<IKIGAI::ECS::ModelLODRenderer::ModelLodRefl> Paths;
	};

	struct ModelLod {
		float m_distance = 0.0;
		std::shared_ptr<RENDER::ModelInterface> m_model;
	};

	ModelLODRenderer(UTILS::Ref<ECS::Object> obj);
	ModelLODRenderer(UTILS::Ref<ECS::Object> obj, const Descriptor _descriptor);
	ModelLODRenderer(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: ModelLODRenderer(obj, static_cast<const Descriptor&>(descriptor)) { };
	void setModel(const ModelLod& model);
	void setModels(const std::vector<ModelLod>& models);
	[[nodiscard]] const std::vector<ModelLod>& getModels() const;
	std::shared_ptr<RENDER::ModelInterface> getModelByDistance(float distance);
	void setFrustumBehaviour(EFrustumBehaviour boundingMode);
	EFrustumBehaviour getFrustumBehaviour() const;
	const RENDER::BoundingSphere& getCustomBoundingSphere() const;
	void setCustomBoundingSphere(const RENDER::BoundingSphere& boundingSphere);
	[[nodiscard]] Descriptor getDescriptor() const;

private:
	void setModelsByPath(std::vector<ModelLodRefl> path);
	std::vector<ModelLodRefl> getModelsPath() const;

	std::vector<ModelLod> m_models;
	RENDER::BoundingSphere m_customBoundingSphere = { {}, 1.0f };
	EFrustumBehaviour m_frustumBehaviour = EFrustumBehaviour::CULL_MODEL;

public:
	static auto GetMembers() {
		return std::tuple {};
	}
};

template <> inline std::string ECS::GetType<ModelLODRenderer>() {
	return "class IKIGAI::ECS::ModelLODRenderer";
}

template <> inline std::string ECS::GetType<ModelRenderer>() {
	return "class IKIGAI::ECS::ModelRenderer";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<ModelLODRenderer>() {
	return "ModelLODRenderer";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<ModelRenderer>() {
	return "ModelRenderer";
}

}

#include "generated/modelRenderer.generated.h"
