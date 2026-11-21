#pragma once

#include "component.h"
#include <renderModule/backends/interface/materialInterface.h>

#include "reflectionModule/reflection_macros.h"
#include "utilsModule/event.h"

#define MAX_MATERIAL_COUNT 255

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class MaterialRenderer : public ComponentBase {
	IKI_GENERATED_BODY(MaterialRenderer)
public:
	IKI_CLASS(Name = MaterialRenderer::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(MaterialRenderer::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "MaterialRendererType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "MaterialNames"))
		std::vector<std::string> MaterialNames;
		IKI_PROPERTY(SEREALIZE(name = "Materials"))
		std::vector<std::string> Materials;
	};

	using MaterialList = std::array<std::shared_ptr<RENDER::MaterialInterface>, MAX_MATERIAL_COUNT>;
	using MaterialNames = std::array<std::string, MAX_MATERIAL_COUNT>;

	MaterialRenderer(UTILS::Ref<ECS::Object> obj);
	MaterialRenderer(UTILS::Ref<ECS::Object> obj, const Descriptor& _descriptor);
	MaterialRenderer(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: MaterialRenderer(obj, static_cast<const Descriptor&>(descriptor)) { };
	~MaterialRenderer() override;
	void fillWithMaterial(std::shared_ptr<RENDER::MaterialInterface> material);
	void setMaterial(unsigned index, std::shared_ptr<RENDER::MaterialInterface> material);
	std::shared_ptr<RENDER::MaterialInterface> GetMaterialAtIndex(unsigned index);
	void removeMaterial(unsigned index);
	void removeMaterial(std::shared_ptr<RENDER::MaterialInterface> instance);
	void removeMaterials();
	void updateMaterialList();
	[[nodiscard]] const MaterialList& getMaterials() const;
	const MaterialNames& getMaterialNames();

	[[nodiscard]] Descriptor getDescriptor() const;

private:
	void setMaterialsByPath(std::vector<std::string> paths);
	std::vector<std::string> getMaterialsPaths() const;
	void setMaterialsNames(std::vector<std::string> paths);
	std::vector<std::string> getMaterialsNames() const;
	MaterialRenderer* getMaterialRenderer();

	MaterialList materials;
	MaterialNames materialNames;

	IKI_PROPERTY(Name = Mterials, Type = IKIGAI::ECS::MaterialRenderer::MaterialList, Getter = getMaterials,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = MATERIAL)

	std::shared_ptr<EVENT::EventListener> setMaterialEventId;

public:
	static void InitReflection() {
		static bool isInit = false;
		if (isInit)
			return;
		isInit = true;

		auto& manager = UTILS::ReflectionManager::Instance();
		manager.registerType<MaterialRenderer>();
		manager.registerField<MaterialRenderer>("materials", &MaterialRenderer::materials);
		manager.registerField<MaterialRenderer>("materialNames", &MaterialRenderer::materialNames);
	}

	static auto GetMembers() {
		return std::tuple { };
	}
};

template <> inline std::string ECS::GetType<MaterialRenderer>() {
	return "class IKIGAI::ECS::MaterialRenderer";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<MaterialRenderer>() {
	return "MaterialRenderer";
}
}

#include "generated/materialRenderer.generated.h"
