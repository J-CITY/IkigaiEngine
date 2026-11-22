#pragma once
#include "identifier.h"
#include "memory/monotonicAllocator.h"
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>

namespace IKIGAI::UTILS {
namespace Detail {
	struct StringPoolTag;
	inline std::uint32_t NextStringPoolToken() {
		static IdSequence<StringPoolTag, std::uint32_t, 1> sequence;
		return sequence.next().value();
	}
}
// Byte-exact, case-sensitive interning. Returned views survive insertion/rehash,
// but not pool destruction. No erasure/reset or implicit Unicode/path normalization.
template <class Hash = std::hash<std::string_view>> class BasicStringPool {
	using IndexEntry = std::pair<const std::string_view, std::uint32_t>;

public:
	BasicStringPool()
		: BasicStringPool(MEMORY::BufferAllocator<MEMORY::Category::General>()) { }
	explicit BasicStringPool(std::shared_ptr<MEMORY::IAllocator> domain, std::size_t pageBytes = 64 * 1024,
		std::size_t budget = std::numeric_limits<std::size_t>::max(), Hash hash = Hash())
		: mToken(Detail::NextStringPoolToken())
		, mBytes(pageBytes, domain, budget)
		, mEntries(MEMORY::StlAllocator<std::string_view>(domain))
		, mIndex(0, std::move(hash), std::equal_to<std::string_view>(), MEMORY::StlAllocator<IndexEntry>(domain)) { }
	BasicStringPool(const BasicStringPool&) = delete;
	BasicStringPool& operator=(const BasicStringPool&) = delete;
	Identifier intern(std::string_view text) {
		if (auto id = find(text); id.valid()) {
			return id;
		}
		if (mEntries.size() >= std::numeric_limits<std::uint32_t>::max()) {
			throw std::overflow_error("String pool exhausted");
		}
		if (mEntries.size() == mEntries.capacity()) {
			mEntries.reserve(MEMORY::Detail::CheckedAdd(mEntries.size(), mEntries.size() / 2 + 1));
		}
		auto* buffer = static_cast<char*>(mBytes.allocate(MEMORY::Detail::CheckedAdd(text.size(), 1), alignof(char)));
		if (!text.empty()) {
			std::memcpy(buffer, text.data(), text.size());
		}
		buffer[text.size()] = '\0';
		std::string_view stored(buffer, text.size());
		const auto index = static_cast<std::uint32_t>(mEntries.size());
		// If the hash table allocation fails, only arena slack is consumed; no
		// partially published ID is visible and old views/IDs remain valid.
		mIndex.emplace(stored, index);
		mEntries.push_back(stored); // capacity prepared; string_view copy cannot throw
		return Identifier(mToken, Id<StringEntryTag>(index));
	}
	Identifier find(std::string_view text) const {
		auto it = mIndex.find(text);
		return it == mIndex.end() ? Identifier {} : Identifier(mToken, Id<StringEntryTag>(it->second));
	}
	std::string_view view(Identifier id) const {
		if (!id.valid() || id.poolToken() != mToken || id.entry().value() >= mEntries.size()) {
			throw std::out_of_range("Foreign or invalid string identifier");
		}
		return mEntries[id.entry().value()];
	}
	std::size_t size() const noexcept {
		return mEntries.size();
	}
	std::uint32_t token() const noexcept {
		return mToken;
	}
	MEMORY::AllocatorStats byteStats() const noexcept {
		return mBytes.stats();
	}

private:
	const std::uint32_t mToken;
	MEMORY::MonotonicAllocator mBytes;
	std::vector<std::string_view, MEMORY::StlAllocator<std::string_view>> mEntries;
	std::unordered_map<std::string_view, std::uint32_t, Hash, std::equal_to<std::string_view>, MEMORY::StlAllocator<IndexEntry>> mIndex;
};
using StringPool = BasicStringPool<>;

// Shared engine consumers use this facade. Reading an already returned view
// needs no lock: its bytes are immutable, and the owner keeps the pool alive.
class SynchronizedStringPool {
public:
	SynchronizedStringPool()
		: SynchronizedStringPool(MEMORY::BufferAllocator<MEMORY::Category::General>()) { }
	explicit SynchronizedStringPool(std::shared_ptr<MEMORY::IAllocator> domain, std::size_t pageBytes = 4096)
		: mPool(std::move(domain), pageBytes) { }
	Identifier intern(std::string_view text) {
		{
			std::shared_lock lock(mMutex);
			if (const auto id = mPool.find(text); id.valid()) {
				return id;
			}
		}
		std::unique_lock lock(mMutex);
		// Another caller may have interned the same bytes while we reacquired the lock.
		return mPool.intern(text);
	}
	Identifier find(std::string_view text) const {
		std::shared_lock lock(mMutex);
		return mPool.find(text);
	}
	std::string_view view(Identifier id) const {
		std::shared_lock lock(mMutex);
		return mPool.view(id);
	}
	std::size_t size() const {
		std::shared_lock lock(mMutex);
		return mPool.size();
	}

private:
	mutable std::shared_mutex mMutex;
	StringPool mPool;
};
}
