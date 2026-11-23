#include "guiComboBoxComponent.h"

#include "coreModule/ecs/componentCatalog.h"
#include "coreModule/ecs/object.h"
#include "spriteComponent.h"
#include "utilsModule/log/loggerDefine.h"
#include "vectorComponents.h"
#include <unordered_set>

namespace IKIGAI::ECS {
namespace {
	[[maybe_unused]] const bool registered = ComponentCatalog::enroll<GuiComboBoxComponent>();
}

GuiComboBoxComponent::GuiComboBoxComponent(UTILS::Ref<Object> object)
	: GuiComboBoxComponent(object, Descriptor {}) { }

GuiComboBoxComponent::GuiComboBoxComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ComponentBase(object)
	, mDescriptor(descriptor) {
	__NAME__ = GetComponentName<GuiComboBoxComponent>();
	mDescriptor.Type = GetType<GuiComboBoxComponent>();
	setSelectedIndex(descriptor.SelectedIndex);
	mValueChanged = false;
}

GuiComboBoxComponent::GuiComboBoxComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: GuiComboBoxComponent(object, static_cast<const Descriptor&>(descriptor)) { }

GuiComboBoxComponent::Descriptor GuiComboBoxComponent::getDescriptor() const {
	return mDescriptor;
}

int GuiComboBoxComponent::getSelectedIndex() const {
	return mDescriptor.SelectedIndex;
}

std::string_view GuiComboBoxComponent::getSelectedText() const {
	const auto index = getSelectedIndex();
	return index >= 0 ? std::string_view(mDescriptor.Options[static_cast<std::size_t>(index)].Text) : std::string_view {};
}

bool GuiComboBoxComponent::isOpen() const {
	return mOpen;
}

void GuiComboBoxComponent::setSelectedIndex(int index) {
	const int selected = index >= 0 && static_cast<std::size_t>(index) < mDescriptor.Options.size() ? index : -1;
	if (mDescriptor.SelectedIndex != selected) {
		mDescriptor.SelectedIndex = selected;
		mValueChanged = true;
	}
}

void GuiComboBoxComponent::onAwake() {
	if (!bindVisuals() && (!mDescriptor.Popup.empty() || !mDescriptor.Options.empty() || !mDescriptor.Label.empty())) {
		IKIGAI_COUT("GuiComboBox '" << obj.get().getName() << "' could not bind its label, popup, or option children");
	}
}

void GuiComboBoxComponent::onDisable() {
	setOpen(false);
}

void GuiComboBoxComponent::onDestroy() {
	if (auto popup = mPopup.lock()) {
		popup->setRuntimeHidden(false);
	}
	auto clearLabel = [](const Object& object) {
		if (auto label = object.getComponent<SdfTextComponent>()) {
			label->clearRuntimeText();
		}
	};
	if (mDescriptor.Label.empty()) {
		clearLabel(obj.get());
	}
	else if (auto label = mLabel.lock()) {
		clearLabel(*label);
	}
	for (const auto& reference : mOptions) {
		if (auto option = reference.lock()) {
			clearLabel(*option);
		}
	}
}

bool GuiComboBoxComponent::bindVisuals() {
	auto& owner = obj.get();
	auto popup = owner.findUniqueChild(mDescriptor.Popup);
	if (popup) {
		mPopup = popup;
		popup->setRuntimeHidden(!mOpen);
	}
	if (!popup && (!mDescriptor.Popup.empty() || !mDescriptor.Options.empty())) {
		return false;
	}
	auto label = owner.findUniqueChild(mDescriptor.Label);
	if (!mDescriptor.Label.empty() && (!label || label == popup || !label->getComponent<SdfTextComponent>())) {
		return false;
	}
	std::vector<std::weak_ptr<Object>> options;
	std::unordered_set<std::string_view> names;
	for (const auto& descriptor : mDescriptor.Options) {
		auto option = popup->findUniqueChild(descriptor.Object);
		if (!option || !option->getComponent<InteractionComponent>() || !names.insert(descriptor.Object).second) {
			return false;
		}
		options.push_back(option);
	}
	mLabel = label;
	mOptions = std::move(options);
	mVisualsBound = true;
	const auto& labelObject = label ? *label : owner;
	if (auto text = labelObject.getComponent<SdfTextComponent>()) {
		text->setRuntimeText(mDescriptor.SelectedIndex < 0 ? mDescriptor.Placeholder : std::string(getSelectedText()));
	}
	for (std::size_t index = 0; index < mOptions.size(); ++index) {
		if (auto option = mOptions[index].lock()) {
			if (auto text = option->getComponent<SdfTextComponent>()) {
				text->setRuntimeText(mDescriptor.Options[index].Text);
			}
		}
	}
	return true;
}

void GuiComboBoxComponent::setOpen(bool open) {
	auto popup = mPopup.lock();
	mOpen = open && popup && !mOptions.empty();
	if (popup) {
		popup->setRuntimeHidden(!mOpen);
	}
}

}
