#include "guiFieldSystem.h"

#include "coreModule/ecs/object.h"
#include "coreModule/gui/components/guiComboBoxComponent.h"
#include "coreModule/gui/components/guiTextInputComponent.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/inputManager/inputManager.h"
#include <algorithm>
#include <utility>

namespace IKIGAI::ECS {
namespace {

	bool activated(const InteractionComponent& interaction) {
		return std::ranges::any_of(interaction.guiEvents(), [](const GUI::Event& event) { return event.type == GUI::EventType::Activate; });
	}

	INPUT_SYSTEM::InputManager* inputManager() {
		return RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputManager>() ? &RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>()
																			  : nullptr;
	}

	bool available(const Object& object, const UTILS::WeakPtr<InteractionComponent>& interaction) {
		return object.isAlive() && object.getIsActive() && object.getIsVisible() && interaction && interaction->mEnabled;
	}

	void publish(CommandBuffer& commands, const UTILS::WeakPtr<InteractionComponent>& interaction, GUI::EventType type) {
		commands.addCommand([interaction, type] {
			if (interaction) {
				interaction->emitGuiEvent({ type });
			}
		});
	}

}

GuiFieldSystem::GuiFieldSystem() {
	mName = "GuiFieldSystem";
	setPhases({ SystemPhase::Update, SystemPhase::Destroy });
	mPriority = 13;
	mExclusiveBatch = true;
	runAfter("GuiInputSystem");
	auto* components = RESOURCES::ServiceManager::Get<World>().getComponentManager();
	mWrites.insert(components->getComponentType<GuiTextInputComponent>());
	mWrites.insert(components->getComponentType<GuiComboBoxComponent>());
	mWrites.insert(components->getComponentType<InteractionComponent>());
	mWrites.insert(components->getComponentType<SdfTextComponent>());
}

void GuiFieldSystem::onUpdate(World& world, CommandBuffer& commands, std::chrono::duration<double>) {
	auto* input = inputManager();
	const bool keyboardAvailable = input && input->hasWindowFocus() && !input->getUiCapture().keyboard;
	bool textInputEnabled = false;
	for (auto& field : world.getComponentManager()->getComponentsArray<GuiTextInputComponent>()) {
		const auto& object = field.getObject();
		if (!field.mLabelBound || (!field.mDescriptor.Label.empty() && field.mLabel.expired())) {
			field.bindLabel();
		}
		auto interaction = object.getComponent<InteractionComponent>();
		const bool focused = available(object, interaction) && interaction->mFocused;
		bool committed = false;
		if (focused && keyboardAvailable) {
			textInputEnabled = true;
			using TextEvent = INPUT_SYSTEM::InputManager::TextInputEvent;
			using Key = INPUT_SYSTEM::EKey;
			for (const auto& event : input->textInputEventsThisFrame()) {
				if (event.type == TextEvent::Type::Commit) {
					field.insert(event.text);
				}
				else if (event.type == TextEvent::Type::Composition) {
					field.setComposition(event.text, event.compositionCursor);
				}
				else if (field.mComposition.empty()) {
					switch (event.key) {
					case Key::KEY_LEFT:
						field.moveCursor(false, event.shift);
						break;
					case Key::KEY_RIGHT:
						field.moveCursor(true, event.shift);
						break;
					case Key::KEY_HOME:
						field.setCursor(0, event.shift);
						break;
					case Key::KEY_END:
						field.setCursor(field.getText().size(), event.shift);
						break;
					case Key::KEY_A:
						if (event.control) {
							field.selectAll();
						}
						break;
					case Key::KEY_ENTER:
					case Key::KEY_KP_ENTER:
						committed = true;
						break;
					case Key::KEY_BACKSPACE:
						field.erase(false);
						break;
					case Key::KEY_DELETE:
						field.erase(true);
						break;
					default:
						break;
					}
				}
			}
		}
		else {
			field.mComposition.clear();
		}
		auto labelChild = field.mLabel.lock();
		const auto* label = field.mDescriptor.Label.empty() ? &object : labelChild.get();
		if (label) {
			if (auto text = label->getComponent<SdfTextComponent>()) {
				text->setRuntimeText(field.displayText(focused && keyboardAvailable));
			}
		}
		if (interaction && std::exchange(field.mValueChanged, false)) {
			publish(commands, interaction, GUI::EventType::ValueChanged);
		}
		if (committed) {
			publish(commands, interaction, GUI::EventType::Activate);
		}
	}
	if (input) {
		input->setTextInputEnabled(textInputEnabled);
	}
	for (auto& combo : world.getComponentManager()->getComponentsArray<GuiComboBoxComponent>()) {
		const auto& object = combo.getObject();
		if (!combo.mVisualsBound || (!combo.mDescriptor.Popup.empty() && combo.mPopup.expired())
			|| (!combo.mDescriptor.Label.empty() && combo.mLabel.expired())) {
			combo.bindVisuals();
		}
		auto interaction = object.getComponent<InteractionComponent>();
		if (!available(object, interaction) || !combo.mVisualsBound) {
			combo.setOpen(false);
		}
		else if (activated(*interaction)) {
			combo.setOpen(!combo.isOpen());
		}
		else if (combo.isOpen()) {
			bool keepOpen = interaction->mFocused;
			for (std::size_t index = 0; index < combo.mOptions.size(); ++index) {
				const auto option = combo.mOptions[index].lock();
				if (!option) {
					continue;
				}
				const auto optionInteraction = option->getComponent<InteractionComponent>();
				if (!available(*option, optionInteraction)) {
					continue;
				}
				if (activated(*optionInteraction)) {
					combo.setSelectedIndex(static_cast<int>(index));
					keepOpen = false;
					break;
				}
				keepOpen = keepOpen || optionInteraction->mFocused || optionInteraction->mPressed;
			}
			combo.setOpen(keepOpen);
		}
		auto labelChild = combo.mLabel.lock();
		const auto* label = combo.mDescriptor.Label.empty() ? &object : labelChild.get();
		if (label) {
			if (auto text = label->getComponent<SdfTextComponent>()) {
				text->setRuntimeText(combo.getSelectedIndex() < 0 ? combo.mDescriptor.Placeholder : std::string(combo.getSelectedText()));
			}
		}
		if (interaction && std::exchange(combo.mValueChanged, false)) {
			publish(commands, interaction, GUI::EventType::ValueChanged);
		}
	}
}

void GuiFieldSystem::onDestroy(World&) {
	if (auto* input = inputManager()) {
		input->setTextInputEnabled(false);
	}
}

}
