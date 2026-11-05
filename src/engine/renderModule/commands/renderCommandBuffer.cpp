#include "renderCommandBuffer.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace IKIGAI::RENDER {
struct RenderCommandBuffer::Impl {
	struct Block {
		std::byte* data = nullptr;
		size_t capacity = 0;
		size_t used = 0;
		size_t alignment = alignof(std::max_align_t);

		Block(size_t bytes, size_t requestedAlignment)
			: capacity(bytes)
			, alignment(std::max(requestedAlignment, alignof(std::max_align_t))) {
			data = static_cast<std::byte*>(::operator new(capacity, std::align_val_t(alignment)));
		}
		~Block() {
			::operator delete(data, std::align_val_t(alignment));
		}
		Block(const Block&) = delete;
		Block& operator=(const Block&) = delete;
	};
	struct Entry {
		CommandHeader* command = nullptr;
		Destroy destroy = nullptr;
	};
	struct BusyGuard {
		Impl& owner;
		explicit BusyGuard(Impl& value)
			: owner(value) {
			owner.busy = true;
		}
		~BusyGuard() {
			owner.busy = false;
		}
	};

	size_t blockSize = 0;
	size_t currentBlock = 0;
	std::vector<std::unique_ptr<Block>> blocks {};
	std::vector<Entry> entries {};
	DriverInterface* boundDriver = nullptr;
	bool busy = false;
	bool cancelRequested = false;

	explicit Impl(size_t bytes)
		: blockSize(bytes) {
		if (!blockSize) {
			throw std::invalid_argument("Render command block size must be positive");
		}
	}

	void checkIdle() const {
		if (busy) {
			throw std::logic_error("Cannot record or replay render commands recursively");
		}
	}
	void checkDriver(DriverInterface* driver) const {
		if (!entries.empty() && boundDriver != driver) {
			throw std::invalid_argument("Render command batch belongs to another driver");
		}
	}
	void* allocate(size_t size, size_t alignment) {
		for (; currentBlock < blocks.size(); ++currentBlock) {
			auto& block = *blocks[currentBlock];
			void* cursor = block.data + block.used;
			size_t available = block.capacity - block.used;
			if (block.alignment >= alignment && std::align(alignment, size, cursor, available)) {
				block.used = static_cast<std::byte*>(cursor) - block.data + size;
				return cursor;
			}
		}
		auto block = std::make_unique<Block>(std::max(size, blockSize), alignment);
		void* result = block->data;
		block->used = size;
		blocks.push_back(std::move(block));
		return result;
	}
	void clear() noexcept {
		for (auto& entry : entries) {
			if (auto* command = std::exchange(entry.command, nullptr)) {
				entry.destroy(command);
			}
		}
		entries.clear();
		for (auto& block : blocks) {
			block->used = 0;
		}
		currentBlock = 0;
		boundDriver = nullptr;
		cancelRequested = false;
	}
};

RenderCommandBuffer::RenderCommandBuffer(size_t blockSize)
	: mImpl(std::make_unique<Impl>(blockSize)) { }

RenderCommandBuffer::~RenderCommandBuffer() {
	cancel();
}

void RenderCommandBuffer::append(DriverInterface* driver, size_t size, size_t alignment, Construct construct, void* context, Destroy destroy) {
	auto& state = *mImpl;
	state.checkIdle();
	state.checkDriver(driver);
	if (size < sizeof(CommandHeader) || alignment < alignof(CommandHeader) || !std::has_single_bit(alignment) || !construct || !destroy) {
		throw std::invalid_argument("Invalid render command allocation");
	}
	Impl::BusyGuard guard(state);
	CommandHeader* command = nullptr;
	try {
		if (state.entries.size() == state.entries.capacity()) {
			const auto capacity = state.entries.capacity();
			const auto maximum = state.entries.max_size();
			if (capacity == maximum) {
				throw std::length_error("Too many render commands");
			}
			state.entries.reserve(std::max(size_t(16), capacity > maximum / 2 ? maximum : capacity * 2));
		}
		command = construct(state.allocate(size, alignment), context);
		if (!command || command->size != size) {
			throw std::logic_error("Render command constructor returned an invalid packet");
		}
		state.entries.push_back({ command, destroy });
		command = nullptr;
		state.boundDriver = driver;
		if (state.cancelRequested) {
			state.clear();
		}
	} catch (...) {
		if (command) {
			destroy(command);
		}
		state.clear();
		throw;
	}
}

void RenderCommandBuffer::execute(DriverInterface* driver, CommandDispatch dispatch) {
	auto& state = *mImpl;
	state.checkIdle();
	state.checkDriver(driver);
	if (!dispatch) {
		throw std::invalid_argument("Render command dispatch is null");
	}
	Impl::BusyGuard guard(state);
	try {
		for (auto& entry : state.entries) {
			auto* command = std::exchange(entry.command, nullptr);
			try {
				dispatch(command, driver);
			} catch (...) {
				entry.destroy(command);
				throw;
			}
			entry.destroy(command);
			if (state.cancelRequested) {
				break;
			}
		}
	} catch (...) {
		state.clear();
		throw;
	}
	state.clear();
}

void RenderCommandBuffer::cancel() noexcept {
	auto& state = *mImpl;
	if (state.busy) {
		state.cancelRequested = true;
		return;
	}
	Impl::BusyGuard guard(state);
	state.clear();
}

size_t RenderCommandBuffer::pendingCommandCount() const noexcept {
	return mImpl->entries.size();
}

size_t RenderCommandBuffer::reservedBytes() const noexcept {
	size_t result = 0;
	for (const auto& block : mImpl->blocks) {
		result += block->capacity;
	}
	return result;
}
}
