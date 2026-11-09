#pragma once
#if USE_IKIGAI_SCRIPT
#include "component.h"
#include "utilsModule/reflection/reflection_macros.h"
#include <cstdint>
#include <memory>
#include <optional>
namespace IKIGAI::SCRIPTING {
class IkigaiScriptRuntime;
}
namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class IkigaiScriptComponent : public ComponentBase {
	IKI_GENERATED_BODY(IkigaiScriptComponent)
public:
	IKI_CLASS(Name = IkigaiScriptComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(IkigaiScriptComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "IkigaiScriptComponentType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "Path"))
		std::string Path;
	};
	explicit IkigaiScriptComponent(UTILS::Ref<Object> object);
	IkigaiScriptComponent(UTILS::Ref<Object> object, const std::string& path);
	IkigaiScriptComponent(UTILS::Ref<Object> object, const Descriptor& descriptor);
	IkigaiScriptComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
		: IkigaiScriptComponent(object, static_cast<const Descriptor&>(descriptor)) { }
	IkigaiScriptComponent(const IkigaiScriptComponent& other);
	IkigaiScriptComponent& operator=(const IkigaiScriptComponent& other);
	IkigaiScriptComponent(IkigaiScriptComponent&&) noexcept;
	IkigaiScriptComponent& operator=(IkigaiScriptComponent&&) noexcept;
	~IkigaiScriptComponent() override;
	const std::string& getScriptName() const {
		return path;
	}
	Descriptor getDescriptor() const;
	bool isScriptOk() const;
	void setScript(const std::string& script);
	void reloadScript();
	void onAwake() override;
	void onStart() override;
	void onEnable() override;
	void onDisable() override;
	void onDestroy() override;
	void onUpdate(std::chrono::duration<double> dt) override;
	void onFixedUpdate(std::chrono::duration<double> dt) override;
	void onLateUpdate(std::chrono::duration<double> dt) override;

private:
	std::string path;
	std::unique_ptr<SCRIPTING::IkigaiScriptRuntime> runtime;
	bool awake = false, started = false, enabled = false;
#if defined(USE_FILE_WATCHER)
	std::optional<std::uint64_t> fileWatchHandle {};
#endif
	void ensureRuntime();
	void attachFileWatch();
	void detachFileWatch();

public:
	IKI_PROPERTY(
		Name = Path, Type = std::string, Getter = getScriptName, Setter = setScript, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = STRING)
	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("Path", &IkigaiScriptComponent::getScriptName, &IkigaiScriptComponent::setScript,
			UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
			{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING } }) };
	}
};
template <> inline std::string GetType<IkigaiScriptComponent>() {
	return "class IKIGAI::ECS::IkigaiScriptComponent";
}
template <> inline std::string GetComponentName<IkigaiScriptComponent>() {
	return "IkigaiScriptComponent";
}
}
#include "generated/ikigaiScriptComponent.generated.h"
#endif
