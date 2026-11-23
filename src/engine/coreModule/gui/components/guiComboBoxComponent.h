#pragma once

#include "coreModule/ecs/components/component.h"
#include <string_view>
#include <vector>

namespace IKIGAI::ECS {

class GuiComboBoxComponent : public ComponentBase {
public:
	struct Option {
		std::string Object;
		std::string Text;

		template <class Context> static constexpr auto serde(Context& context, Option& value) {
			serde::serde_struct(context, value).field(&Option::Object, "Object").field(&Option::Text, "Text");
		}
	};

	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int SelectedIndex = -1;
		std::string Placeholder;
		std::string Label;
		std::string Popup;
		std::vector<Option> Options;

		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "GuiComboBoxComponentType")
				.field(&Descriptor::SelectedIndex, "SelectedIndex", default_ { -1 })
				.field(&Descriptor::Placeholder, "Placeholder", default_ { std::string {} })
				.field(&Descriptor::Label, "Label", default_ { std::string {} })
				.field(&Descriptor::Popup, "Popup", default_ { std::string {} })
				.field(&Descriptor::Options, "Options", default_ { std::vector<Option> {} });
		}
	};

	explicit GuiComboBoxComponent(UTILS::Ref<Object> object);
	GuiComboBoxComponent(UTILS::Ref<Object> object, const Descriptor& descriptor);
	GuiComboBoxComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;
	[[nodiscard]] int getSelectedIndex() const;
	[[nodiscard]] std::string_view getSelectedText() const;
	[[nodiscard]] bool isOpen() const;
	void setSelectedIndex(int index);
	static auto GetMembers() {
		return std::tuple { UTILS::MakeMemberInfo("SelectedIndex", &GuiComboBoxComponent::getSelectedIndex, &GuiComboBoxComponent::setSelectedIndex,
			UTILS::Meta_t { { UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
			{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_INT } }) };
	}

	void onAwake() override;
	void onDisable() override;
	void onDestroy() override;

private:
	friend class GuiFieldSystem;
	bool bindVisuals();
	void setOpen(bool open);

	Descriptor mDescriptor;
	std::weak_ptr<Object> mLabel;
	std::weak_ptr<Object> mPopup;
	std::vector<std::weak_ptr<Object>> mOptions;
	bool mVisualsBound = false;
	bool mOpen = false;
	bool mValueChanged = false;
};

template <> inline std::string GetType<GuiComboBoxComponent>() {
	return "class IKIGAI::ECS::GuiComboBoxComponent";
}
template <> inline std::string GetComponentName<GuiComboBoxComponent>() {
	return "GuiComboBoxComponent";
}

}
