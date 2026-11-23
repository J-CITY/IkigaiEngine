#include "guiTextInputComponent.h"

#include "coreModule/ecs/componentCatalog.h"
#include "coreModule/ecs/object.h"
#include "utilsModule/log/loggerDefine.h"
#include "vectorComponents.h"
#include "vectorGraphicsModule/text/fontResource.h"
#include <algorithm>
#include <limits>

namespace IKIGAI::ECS {
namespace {
	[[maybe_unused]] const bool registered = ComponentCatalog::enroll<GuiTextInputComponent>();

	bool isContinuation(char value) {
		return (static_cast<unsigned char>(value) & 0xC0) == 0x80;
	}

	std::size_t nextCharacter(std::string_view text, std::size_t cursor) {
		if (cursor < text.size()) {
			++cursor;
			while (cursor < text.size() && isContinuation(text[cursor])) {
				++cursor;
			}
		}
		return cursor;
	}

	std::size_t previousCharacter(std::string_view text, std::size_t cursor) {
		if (cursor > 0) {
			--cursor;
			while (cursor > 0 && isContinuation(text[cursor])) {
				--cursor;
			}
		}
		return cursor;
	}

	std::size_t characterCount(std::string_view text) {
		return std::ranges::count_if(text, [](char byte) { return !isContinuation(byte); });
	}

	void appendCodePoint(std::string& text, std::uint32_t codePoint) {
		if (codePoint < 0x80) {
			text.push_back(static_cast<char>(codePoint));
			return;
		}
		const int tailBytes = codePoint < 0x800 ? 1 : (codePoint < 0x10000 ? 2 : 3);
		const unsigned prefix = tailBytes == 1 ? 0xC0 : (tailBytes == 2 ? 0xE0 : 0xF0);
		text.push_back(static_cast<char>(prefix | (codePoint >> (tailBytes * 6))));
		for (int index = tailBytes - 1; index >= 0; --index) {
			text.push_back(static_cast<char>(0x80 | ((codePoint >> (index * 6)) & 0x3F)));
		}
	}

	std::string singleLineText(std::string_view text, std::size_t limit = std::numeric_limits<std::size_t>::max()) {
		std::string result;
		for (const auto codePoint : VECTOR::decodeUtf8(std::string(text))) {
			if (limit == 0) {
				break;
			}
			if (codePoint >= 0x20 && !(codePoint >= 0x7F && codePoint < 0xA0) && codePoint != 0x2028 && codePoint != 0x2029) {
				appendCodePoint(result, codePoint);
				--limit;
			}
		}
		return result;
	}

}

GuiTextInputComponent::GuiTextInputComponent(UTILS::Ref<Object> object)
	: GuiTextInputComponent(object, Descriptor {}) { }

GuiTextInputComponent::GuiTextInputComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ComponentBase(object)
	, mDescriptor(descriptor) {
	__NAME__ = GetComponentName<GuiTextInputComponent>();
	mDescriptor.Type = GetType<GuiTextInputComponent>();
	mDescriptor.MaxLength = std::max(mDescriptor.MaxLength, 0);
	setText(descriptor.Text);
	mValueChanged = false;
}

GuiTextInputComponent::GuiTextInputComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: GuiTextInputComponent(object, static_cast<const Descriptor&>(descriptor)) { }

GuiTextInputComponent::Descriptor GuiTextInputComponent::getDescriptor() const {
	return mDescriptor;
}

const std::string& GuiTextInputComponent::getText() const {
	return mDescriptor.Text;
}

void GuiTextInputComponent::setText(std::string_view text) {
	const auto limit = mDescriptor.MaxLength > 0 ? static_cast<std::size_t>(mDescriptor.MaxLength) : std::numeric_limits<std::size_t>::max();
	auto value = singleLineText(text, limit);
	if (mDescriptor.Text != value) {
		mDescriptor.Text = std::move(value);
		mValueChanged = true;
	}
	setCursor(mDescriptor.Text.size(), false);
	mComposition.clear();
}

void GuiTextInputComponent::onAwake() {
	if (!bindLabel() && !mDescriptor.Label.empty()) {
		IKIGAI_COUT("GuiTextInput '" << obj.get().getName() << "' needs an SDF text child named '" << mDescriptor.Label << "'");
	}
}

void GuiTextInputComponent::onDestroy() {
	auto child = mLabel.lock();
	const auto* label = mDescriptor.Label.empty() ? &obj.get() : child.get();
	if (label) {
		if (auto text = label->getComponent<SdfTextComponent>()) {
			text->clearRuntimeText();
		}
	}
}

bool GuiTextInputComponent::bindLabel() {
	auto& owner = obj.get();
	auto child = owner.findUniqueChild(mDescriptor.Label);
	const auto* label = mDescriptor.Label.empty() ? &owner : child.get();
	if (!label) {
		return false;
	}
	if (auto text = label->getComponent<SdfTextComponent>()) {
		mLabel = child;
		mLabelBound = true;
		text->setRuntimeText(displayText(false));
		return true;
	}
	return false;
}

std::pair<std::size_t, std::size_t> GuiTextInputComponent::selection() const {
	return std::minmax(mCursor, mSelectionAnchor);
}

void GuiTextInputComponent::setCursor(std::size_t position, bool extendSelection) {
	mCursor = position;
	if (!extendSelection) {
		mSelectionAnchor = mCursor;
	}
}

void GuiTextInputComponent::moveCursor(bool forward, bool extendSelection) {
	const auto [start, end] = selection();
	if (!extendSelection && start != end) {
		setCursor(forward ? end : start, false);
		return;
	}
	setCursor(forward ? nextCharacter(mDescriptor.Text, mCursor) : previousCharacter(mDescriptor.Text, mCursor), extendSelection);
}

void GuiTextInputComponent::selectAll() {
	mSelectionAnchor = 0;
	setCursor(mDescriptor.Text.size(), true);
}

void GuiTextInputComponent::erase(bool forward) {
	auto& text = mDescriptor.Text;
	auto [start, end] = selection();
	if (start == end) {
		start = forward ? mCursor : previousCharacter(text, mCursor);
		end = forward ? nextCharacter(text, mCursor) : mCursor;
	}
	if (start != end) {
		text.erase(start, end - start);
		setCursor(start, false);
		mValueChanged = true;
	}
}

void GuiTextInputComponent::insert(std::string_view text) {
	const auto [start, end] = selection();
	const std::string_view current = mDescriptor.Text;
	const auto selected = current.substr(start, end - start);
	auto limit = std::numeric_limits<std::size_t>::max();
	if (mDescriptor.MaxLength > 0) {
		limit = static_cast<std::size_t>(mDescriptor.MaxLength) - (characterCount(current) - characterCount(selected));
	}
	auto inserted = singleLineText(text, limit);
	if (!inserted.empty()) {
		if (inserted != selected) {
			mDescriptor.Text.replace(start, end - start, inserted);
			mValueChanged = true;
		}
		setCursor(start + inserted.size(), false);
	}
	mComposition.clear();
}

void GuiTextInputComponent::setComposition(std::string_view text, int cursor) {
	mComposition = singleLineText(text);
	mCompositionCursor = cursor < 0 ? mComposition.size() : 0;
	for (int index = 0; index < cursor && mCompositionCursor < mComposition.size(); ++index) {
		mCompositionCursor = nextCharacter(mComposition, mCompositionCursor);
	}
}

std::string GuiTextInputComponent::displayText(bool focused) const {
	if (!focused) {
		return mDescriptor.Text.empty() ? mDescriptor.Placeholder : mDescriptor.Text;
	}
	auto display = mDescriptor.Text;
	const auto [start, end] = selection();
	if (!mComposition.empty()) {
		display.replace(start, end - start, mComposition);
		display.insert(start + mCompositionCursor, 1, '|');
	}
	else if (start != end) {
		display.insert(end, 1, ']');
		display.insert(start, 1, '[');
	}
	else {
		display.insert(mCursor, 1, '|');
	}
	return display;
}

}
