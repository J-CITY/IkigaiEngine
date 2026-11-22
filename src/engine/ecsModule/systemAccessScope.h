#pragma once

#include "commandBuffer.h"
#include "componentHandle.h"
#include "utilsModule/taskBorrowScope.h"
#include <optional>
#include <string_view>
#include <typeindex>
#include <unordered_set>

namespace IKIGAI::ECS {

enum class AccessMode { Read, Write };

// Metadata is borrowed for the system callback and its synchronously joined ranges.
class SystemAccessScope {
public:
	~SystemAccessScope();

	SystemAccessScope(const SystemAccessScope&) = delete;
	SystemAccessScope& operator=(const SystemAccessScope&) = delete;

	static void requireComponent(const ComponentManager& manager, ComponentType type, AccessMode mode);
	static void requireResource(std::type_index type, AccessMode mode);
	static const SystemAccessScope& current();
	static void requireWorld(const ComponentManager& manager);
	static void requireStorageAccess(const ComponentManager& manager);
	static void requireCommandBuffer(const CommandBuffer& commands);

private:
	friend class ComponentManager;
	friend class SystemManager;
	friend struct SystemRangeExecutor;
	// A custom executor can run on the owner while its workers hold component borrows.
	class BatchDispatch {
	public:
		explicit BatchDispatch(const ComponentManager& manager) noexcept;
		~BatchDispatch();
		BatchDispatch(const BatchDispatch&) = delete;
		BatchDispatch& operator=(const BatchDispatch&) = delete;

	private:
		friend class SystemAccessScope;
		const ComponentManager& mManager;
		const BatchDispatch* mPrevious;
		const SystemAccessScope* mPreviousScope;
	};
	static thread_local const BatchDispatch* sDispatch;
	static bool hasSystemScopeOrRequireOwner(const ComponentManager& manager);
	SystemAccessScope(bool enabled, std::string_view system, const ComponentManager& manager, const std::unordered_set<ComponentType>& reads,
		const std::unordered_set<ComponentType>& writes, const std::unordered_set<std::type_index>& resourceReads,
		const std::unordered_set<std::type_index>& resourceWrites) noexcept;
	explicit SystemAccessScope(const SystemAccessScope& parent, CommandBuffer& childCommands) noexcept;
	static thread_local const SystemAccessScope* sCurrent;
	const SystemAccessScope* mPrevious = nullptr;
	std::string_view mSystem;
	const ComponentManager& mManager;
	const std::unordered_set<ComponentType>& mReads;
	const std::unordered_set<ComponentType>& mWrites;
	const std::unordered_set<std::type_index>& mResourceReads;
	const std::unordered_set<std::type_index>& mResourceWrites;
	const bool mChecked;
	UTILS::TaskBorrowScope mBorrowScope { UTILS::TaskBorrowState::Borrowing };
	std::optional<ActiveCommandBufferScope> mChildCommands;
};
}
