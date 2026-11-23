#pragma once

#include "coreModule/ecs/components/component.h"
#include <cstddef>
#include <string_view>
#include <utility>

namespace IKIGAI::ECS {

class GuiTextInputComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		std::string Text;
		std::string Placeholder;
		std::string Label;
		int MaxLength = 0;

		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "GuiTextInputComponentType")
				.field(&Descriptor::Text, "Text", default_ { std::string {} })
				.field(&Descriptor::Placeholder, "Placeholder", default_ { std::string {} })
				.field(&Descriptor::Label, "Label", default_ { std::string {} })
				.field(&Descriptor::MaxLength, "MaxLength", default_ { 0 });
		}
	};

	explicit GuiTextInputComponent(UTILS::Ref<Object> object);
	GuiTextInputComponent(UTILS::Ref<Object> object, const Descriptor& descriptor);
	GuiTextInputComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	[[nodiscard]] Descriptor getDescriptor() const;
	[[nodiscard]] const std::string& getText() const;
	void setText(std::string_view text);
	static auto GetMembers() {
		return std::tuple {};
	}

	void onAwake() override;
	void onDestroy() override;

private:
	friend class GuiFieldSystem;
	bool bindLabel();
	void moveCursor(bool forward, bool extendSelection);
	void setCursor(std::size_t position, bool extendSelection);
	void selectAll();
	[[nodiscard]] std::pair<std::size_t, std::size_t> selection() const;
	void erase(bool forward);
	void insert(std::string_view text);
	void setComposition(std::string_view text, int cursor);
	std::string displayText(bool focused) const;

	Descriptor mDescriptor;
	std::weak_ptr<Object> mLabel;
	std::string mComposition;
	std::size_t mCompositionCursor = 0;
	std::size_t mCursor = 0;
	std::size_t mSelectionAnchor = 0;
	bool mLabelBound = false;
	bool mValueChanged = false;
};

template <> inline std::string GetType<GuiTextInputComponent>() {
	return "class IKIGAI::ECS::GuiTextInputComponent";
}
template <> inline std::string GetComponentName<GuiTextInputComponent>() {
	return "GuiTextInputComponent";
}

}
