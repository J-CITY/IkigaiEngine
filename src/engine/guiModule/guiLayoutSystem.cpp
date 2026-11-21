#include "guiLayoutSystem.h"

#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace IKIGAI::ECS {
namespace {
	MATH::Vector2f layoutElementSize(const Object& object) {
		if (const auto layout = object.getComponent<LayoutComponent>()) {
			if (layout->mContentSize.x > 0.0f || layout->mContentSize.y > 0.0f) {
				return layout->mContentSize;
			}
		}
		const auto& size = object.getTransform()->getTransform().getLocalSize();
		if (size.x > 0.0f || size.y > 0.0f) {
			return size;
		}
		if (const auto interaction = object.getComponent<InteractionComponent>()) {
			return { interaction->mWidth, interaction->mHeight };
		}
		if (const auto scroll = object.getComponent<ScrollComponent>()) {
			return { scroll->mWidth, scroll->mHeight };
		}
		if (const auto shape = object.getComponent<VectorShapeComponent>()) {
			return shape->Size;
		}
		return { 0.0f, 0.0f };
	}

	int objectDepth(const Object& object) {
		int depth = 0;
		for (auto parent = object.getParent(); parent; parent = parent->getParent()) {
			++depth;
		}
		return depth;
	}

	std::size_t hashCombine(std::size_t seed, std::size_t value) {
		return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
	}

	std::size_t layoutSignature(Object& object, const LayoutComponent& layout) {
		std::size_t signature = std::hash<int> {}(static_cast<int>(layout.mType));
		signature = hashCombine(signature, std::hash<int> {}(static_cast<int>(layout.mAlignment)));
		signature = hashCombine(signature, std::hash<float> {}(layout.mHorizontalOffset));
		signature = hashCombine(signature, std::hash<float> {}(layout.mVerticalOffset));

		const auto children = object.getChildren();
		signature = hashCombine(signature, children.size());
		for (const auto& child : children) {
			if (!child || !child->getIsVisible()) {
				continue;
			}
			const auto childSize = layoutElementSize(*child);
			signature = hashCombine(signature, std::hash<float> {}(childSize.x));
			signature = hashCombine(signature, std::hash<float> {}(childSize.y));
		}
		return signature;
	}

	float crossOffset(LayoutComponent::Alignment alignment, float band, float element) {
		switch (alignment) {
		case LayoutComponent::Alignment::CENTER:
			return (band - element) * 0.5f;
		case LayoutComponent::Alignment::END:
			return band - element;
		case LayoutComponent::Alignment::START:
		default:
			return 0.0f;
		}
	}

	MATH::Vector2f measureLayout(Object& object, LayoutComponent& layout) {
		float mainExtent = 0.0f;
		float crossExtent = 0.0f;
		int visibleChildren = 0;

		for (const auto& child : object.getChildren()) {
			if (!child || !child->getIsVisible()) {
				continue;
			}
			const auto childSize = layoutElementSize(*child);
			if (layout.mType == LayoutComponent::Type::HORIZONTAL) {
				mainExtent += childSize.x;
				crossExtent = std::max(crossExtent, childSize.y);
			}
			else {
				mainExtent += childSize.y;
				crossExtent = std::max(crossExtent, childSize.x);
			}
			++visibleChildren;
		}

		if (visibleChildren > 1) {
			const float gap = layout.mType == LayoutComponent::Type::HORIZONTAL ? layout.mHorizontalOffset : layout.mVerticalOffset;
			mainExtent += gap * static_cast<float>(visibleChildren - 1);
		}

		if (layout.mType == LayoutComponent::Type::HORIZONTAL) {
			return { mainExtent, crossExtent };
		}
		return { crossExtent, mainExtent };
	}

	void positionLayout(Object& object, LayoutComponent& layout, const MATH::Vector2f& contentSize) {
		float cursorMain = 0.0f;
		const float gap = layout.mType == LayoutComponent::Type::HORIZONTAL ? layout.mHorizontalOffset : layout.mVerticalOffset;
		const float crossShift = layout.mType == LayoutComponent::Type::HORIZONTAL ? layout.mVerticalOffset : layout.mHorizontalOffset;
		const float crossBand = layout.mType == LayoutComponent::Type::HORIZONTAL ? contentSize.y : contentSize.x;
		bool first = true;

		for (const auto& child : object.getChildren()) {
			if (!child || !child->getIsVisible()) {
				continue;
			}
			if (!first) {
				cursorMain += gap;
			}
			first = false;

			const auto childSize = layoutElementSize(*child);
			const float crossPos
				= crossOffset(layout.mAlignment, crossBand, layout.mType == LayoutComponent::Type::HORIZONTAL ? childSize.y : childSize.x)
				+ crossShift;

			MATH::Vector3f localPos {};
			if (layout.mType == LayoutComponent::Type::HORIZONTAL) {
				localPos = { cursorMain, crossPos, 0.0f };
				cursorMain += childSize.x;
			}
			else {
				localPos = { crossPos, cursorMain, 0.0f };
				cursorMain += childSize.y;
			}
			child->getTransform()->getTransform().setLocalPosition(localPos);
		}
	}
}

GuiLayoutSystem::GuiLayoutSystem() {
	mName = "GuiLayoutSystem";
	mPriority = 5;
	mExclusiveBatch = true;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mReads.insert(cm->getComponentType<LayoutComponent>());
	mWrites.insert(cm->getComponentType<LayoutComponent>());
}

void GuiLayoutSystem::onUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) {
	(void)cb;
	(void)dt;

	struct Entry {
		LayoutComponent* layout = nullptr;
		Object* object = nullptr;
		int depth = 0;
	};

	std::vector<Entry> entries;
	for (auto& layout : world.getComponentManager()->getComponentsArray<LayoutComponent>()) {
		auto& object = const_cast<Object&>(layout.getObject());
		if (!object.getIsVisible()) {
			continue;
		}
		entries.push_back({ &layout, &object, objectDepth(object) });
	}

	if (entries.empty()) {
		return;
	}

	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.depth > b.depth; });
	for (auto& entry : entries) {
		const auto signature = layoutSignature(*entry.object, *entry.layout);
		if (!entry.layout->mDirty && signature == entry.layout->mLastSignature) {
			continue;
		}
		entry.layout->mContentSize = measureLayout(*entry.object, *entry.layout);
		entry.layout->mLastSignature = signature;
		entry.layout->mDirty = false;
	}

	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.depth < b.depth; });
	for (auto& entry : entries) {
		const auto signature = layoutSignature(*entry.object, *entry.layout);
		if (signature != entry.layout->mLastSignature) {
			entry.layout->mContentSize = measureLayout(*entry.object, *entry.layout);
			entry.layout->mLastSignature = signature;
		}
		positionLayout(*entry.object, *entry.layout, entry.layout->mContentSize);
	}
}
}
