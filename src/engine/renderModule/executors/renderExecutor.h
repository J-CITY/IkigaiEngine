#pragma once
#include "renderModule/commands/renderCommandBuffer.h"
#include <new>
#include <type_traits>
#include <utility>

namespace IKIGAI::RENDER {
class RenderExecutorInterface {
public:
	virtual ~RenderExecutorInterface() = default;
	virtual void flush(DriverInterface* driver) = 0;
	void cancel() noexcept;
	[[nodiscard]] size_t pendingCommandCount() const noexcept;
	[[nodiscard]] size_t reservedBytes() const noexcept;
	void beginFrame(std::shared_ptr<RenderCommandBuffer> commands);
	void endFrame();
	[[nodiscard]] bool isExecuting() const noexcept;

	template <typename T, typename... Args> void submit(DriverInterface* driver, OpCode op, Args&&... args) {
		static_assert(std::is_base_of_v<CommandHeader, T> && std::is_aggregate_v<T>);
		static_assert(std::is_nothrow_destructible_v<T>);
		auto construct = [&](void* storage) -> CommandHeader* {
			if constexpr (std::is_same_v<T, CommandHeader>) {
				return ::new (storage) T { op, sizeof(T) };
			}
			else {
				return ::new (storage) T { { op, sizeof(T) }, std::forward<Args>(args)... };
			}
		};
		mCommands->append(
			driver, sizeof(T), alignof(T), [](void* storage, void* context) { return (*static_cast<decltype(construct)*>(context))(storage); },
			&construct, [](CommandHeader* command) noexcept { std::destroy_at(static_cast<T*>(command)); });
		onSubmitted(driver);
	}

protected:
	explicit RenderExecutorInterface(CommandDispatch dispatch, size_t blockSize);
	void executePending(DriverInterface* driver);
	virtual void onSubmitted(DriverInterface* driver) = 0;

private:
	CommandDispatch mDispatch = nullptr;
	std::shared_ptr<RenderCommandBuffer> mStandaloneCommands;
	std::shared_ptr<RenderCommandBuffer> mCommands;
};

class ImmediateExecutor final : public RenderExecutorInterface {
public:
	explicit ImmediateExecutor(CommandDispatch dispatch = DispatchRenderCommand, size_t blockSize = 64 * 1024);
	void flush(DriverInterface* driver) override;

private:
	void onSubmitted(DriverInterface* driver) override;
};

class BufferedExecutor final : public RenderExecutorInterface {
public:
	explicit BufferedExecutor(CommandDispatch dispatch = DispatchRenderCommand, size_t blockSize = 64 * 1024);
	void flush(DriverInterface* driver) override;

private:
	void onSubmitted(DriverInterface* driver) override;
};
}
