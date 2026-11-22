#pragma once

namespace IKIGAI::UTILS {
// A callback borrowing caller-owned state can submit snapshot work. Ordinary
// jobs keep only this restriction, never a pointer into the caller's stack.
enum class TaskBorrowState { Unrestricted, Borrowing, Detached };

class TaskBorrowScope {
public:
	explicit TaskBorrowScope(TaskBorrowState state) noexcept
		: mPrevious(sCurrent) {
		// Helping an unrelated job must also hide the waiting callback's borrows.
		sCurrent = state == TaskBorrowState::Unrestricted ? forChild() : state;
	}
	~TaskBorrowScope() {
		sCurrent = mPrevious;
	}
	TaskBorrowScope(const TaskBorrowScope&) = delete;
	TaskBorrowScope& operator=(const TaskBorrowScope&) = delete;

	static TaskBorrowState current() noexcept {
		return sCurrent;
	}
	static TaskBorrowState forChild() noexcept {
		return sCurrent == TaskBorrowState::Unrestricted ? sCurrent : TaskBorrowState::Detached;
	}

private:
	inline static thread_local TaskBorrowState sCurrent = TaskBorrowState::Unrestricted;
	TaskBorrowState mPrevious;
};
}
