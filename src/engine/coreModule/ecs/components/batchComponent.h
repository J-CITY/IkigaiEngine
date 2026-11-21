#pragma once
#include "component.h"
#include "materialRenderer.h"
#include "modelRenderer.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class BatchComponent : public ComponentBase {
	IKI_GENERATED_BODY(BatchComponent)
public:
	IKI_CLASS(Name = BatchComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(BatchComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "BatchComponentType"))
		std::string Type;
	};
	inline static std::vector<unsigned> ids;
	BatchComponent(UTILS::Ref<ECS::Object> obj);
	BatchComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: BatchComponent(obj) { };

	[[nodiscard]] Descriptor getDescriptor() const;
#ifdef OPENGL_BACKEND
	void init();
	void CreateAtlases(const MaterialRenderer& material);
	void createBuffers(ModelRenderer& model, MaterialRenderer& material);
#endif
public:
	static auto GetMembers() {
		return std::tuple { };
	}
};

template <> inline std::string ECS::GetType<BatchComponent>() {
	return "class IKIGAI::ECS::BatchComponent";
}
template <> inline std::string IKIGAI::ECS::GetComponentName<BatchComponent>() {
	return "BatchComponent";
}
}

#include "generated/batchComponent.generated.h"
