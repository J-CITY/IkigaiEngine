#pragma once
#include "entityManager.h"
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace IKIGAI::ECS {
class ComponentManager;
class World;

class CommandBuffer {
	struct Recording { };

public:
	// A placeholder valid only while recording into the buffer that created it.
	class PendingEntity {
	public:
		PendingEntity() = default;

	private:
		friend class CommandBuffer;
		PendingEntity(const std::shared_ptr<const Recording>& recording, std::size_t index)
			: mRecording(recording)
			, mIndex(index) { }

		std::weak_ptr<const Recording> mRecording;
		std::size_t mIndex = 0;
	};

	using EntityTarget = std::variant<EntityHandle, PendingEntity>;

	[[nodiscard]] PendingEntity createEntity();

	// Own the constructor arguments until playback, including move-only values.
	template <class T, class... Args> void addComponent(EntityTarget target, Args&&... args) {
		auto add = [arguments = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)](auto& components, Entity entity) mutable {
			std::apply([&](auto&&... values) { components.addComponents(entity, T { std::move(values)... }); }, std::move(arguments));
		};
		queueEntityEdit(target, std::make_unique<TypedEdit<decltype(add)>>(std::move(add)));
	}

	template <class T> void removeComponent(EntityTarget target) {
		auto remove = [](auto& components, Entity entity) { components.template removeComponents<T>(entity); };
		queueEntityEdit(target, std::make_unique<TypedEdit<decltype(remove)>>(std::move(remove)));
	}

	void destroyEntity(EntityTarget target);
	void addCommand(std::function<void()>&& command);

	// Play back on the owning thread after the batch has finished. Stale or foreign
	// entity handles are skipped. If a command throws, later commands are discarded.
	void execute(World& world);
	// Legacy callback-only playback.
	void execute();

private:
	struct EntityEdit {
		virtual ~EntityEdit() = default;
		virtual void apply(ComponentManager& components, Entity entity) = 0;
	};

	template <class Function> struct TypedEdit final : EntityEdit {
		explicit TypedEdit(Function&& apply)
			: function(std::move(apply)) { }

		void apply(ComponentManager& components, Entity entity) override {
			function(components, entity);
		}

		Function function;
	};

	struct CreateEntity { };
	struct EditEntity {
		std::variant<EntityHandle, std::size_t> target;
		std::unique_ptr<EntityEdit> edit;
	};
	using Command = std::variant<std::function<void()>, CreateEntity, EditEntity>;

	void queueEntityEdit(const EntityTarget& target, std::unique_ptr<EntityEdit> edit);
	void executeImpl(World* world);

	std::vector<Command> mCommands;
	std::shared_ptr<const Recording> mRecording;
	std::size_t mPendingCount = 0;
	std::mutex mMutex;
};

inline CommandBuffer*& activeCommandBuffer() {
	thread_local CommandBuffer* current = nullptr;
	return current;
}

inline bool& immediateStructuralChangesAllowed() {
	thread_local bool allowed = true;
	return allowed;
}

class ActiveCommandBufferScope {
public:
	explicit ActiveCommandBufferScope(CommandBuffer& buffer, bool allowImmediate = true)
		: mPrevious(activeCommandBuffer())
		, mPreviousAllowImmediate(immediateStructuralChangesAllowed()) {
		immediateStructuralChangesAllowed() = mPreviousAllowImmediate && allowImmediate;
		activeCommandBuffer() = &buffer;
	}

	~ActiveCommandBufferScope() {
		activeCommandBuffer() = mPrevious;
		immediateStructuralChangesAllowed() = mPreviousAllowImmediate;
	}

	ActiveCommandBufferScope(const ActiveCommandBufferScope&) = delete;
	ActiveCommandBufferScope& operator=(const ActiveCommandBufferScope&) = delete;
	ActiveCommandBufferScope(ActiveCommandBufferScope&&) = delete;
	ActiveCommandBufferScope& operator=(ActiveCommandBufferScope&&) = delete;

private:
	CommandBuffer* mPrevious = nullptr;
	bool mPreviousAllowImmediate = true;
};
}
