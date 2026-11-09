#include "commandBuffer.h"

#include "componentManager.h"
#include "world.h"

#include <algorithm>
#include <stdexcept>

namespace IKIGAI::ECS {
CommandBuffer::PendingEntity CommandBuffer::createEntity() {
	std::lock_guard guard(mMutex);
	if (!mRecording) {
		mRecording = std::make_shared<Recording>();
	}
	mCommands.emplace_back(CreateEntity {});
	return { mRecording, mPendingCount++ };
}

void CommandBuffer::queueEntityEdit(const EntityTarget& target, std::unique_ptr<EntityEdit> edit) {
	std::lock_guard guard(mMutex);
	if (const auto* pending = std::get_if<PendingEntity>(&target)) {
		if (!mRecording || pending->mRecording.lock() != mRecording) {
			throw std::logic_error("Pending ECS entity belongs to another command buffer recording");
		}
		mCommands.emplace_back(EditEntity { pending->mIndex, std::move(edit) });
		return;
	}
	mCommands.emplace_back(EditEntity { std::get<EntityHandle>(target), std::move(edit) });
}

void CommandBuffer::destroyEntity(EntityTarget target) {
	auto destroy = [](ComponentManager& components, Entity entity) { components.destroyEntity(entity); };
	queueEntityEdit(target, std::make_unique<TypedEdit<decltype(destroy)>>(std::move(destroy)));
}

void CommandBuffer::addCommand(std::function<void()>&& command) {
	std::lock_guard guard(mMutex);
	mCommands.emplace_back(std::move(command));
}

void CommandBuffer::execute(World& world) {
	executeImpl(&world);
}

void CommandBuffer::execute() {
	executeImpl(nullptr);
}

void CommandBuffer::executeImpl(World* world) {
	std::vector<Command> commands;
	std::vector<EntityHandle> createdEntities;
	{
		std::lock_guard guard(mMutex);
		if ((!world || activeCommandBuffer())
			&& std::ranges::any_of(mCommands, [](const Command& command) { return !std::holds_alternative<std::function<void()>>(command); })) {
			throw std::logic_error(
				!world ? "ECS structural command playback requires a world" : "ECS structural command playback must occur outside system execution");
		}
		createdEntities.reserve(mPendingCount);
		commands.swap(mCommands);
		mRecording.reset();
		mPendingCount = 0;
	}

	for (auto& command : commands) {
		if (auto* callback = std::get_if<std::function<void()>>(&command)) {
			(*callback)();
			continue;
		}

		auto& components = *world->getComponentManager();
		if (std::holds_alternative<CreateEntity>(command)) {
			createdEntities.push_back(components.getEntityHandle(world->createEntity()));
			continue;
		}

		auto& edit = std::get<EditEntity>(command);
		const auto* handle = std::get_if<EntityHandle>(&edit.target);
		const auto entity = components.resolveEntity(handle ? *handle : createdEntities[std::get<std::size_t>(edit.target)]);
		if (entity) {
			edit.edit->apply(components, *entity);
		}
	}
}
}
