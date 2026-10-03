#pragma once
#include <cstddef>
#include <climits>
#include <functional>

namespace IKIGAI {
	template<typename T>
	class Id {
	public:
		explicit Id(unsigned int id) noexcept : mId(id) {}

		explicit operator unsigned int() const noexcept {
			return mId;
		}

		explicit operator int() const noexcept {
			return static_cast<int>(mId);
		}

#if SIZE_MAX != UINT_MAX
		explicit operator size_t() const noexcept {
			return static_cast<size_t>(mId);
		}
#endif

		bool operator<(const Id& b) const noexcept {
			return mId < b.mId;
		}

		bool operator==(const Id& b) const noexcept {
			return mId == b.mId;
		}

		bool operator!=(const Id& b) const noexcept {
			return mId != b.mId;
		}

	private:
		int mId;
	};

	template<typename T>
	class IdGenerator {
	public:
		using ID = Id<T>;

		IdGenerator() : mId(generateId()) {}

		IdGenerator(ID id) : mId(id) {
			if (CurrentID <= static_cast<int>(mId)) {
				CurrentID = static_cast<int>(mId) + 1;
			}
		}

		ID getUniqueId() const noexcept {
			return mId;
		}

		static ID generateId() noexcept {
			return ID(CurrentID++);
		}

	protected:
		inline static int CurrentID = 0;
		ID mId;
	};

}

namespace std {
	template<typename T>
	struct hash<IKIGAI::Id<T>> {
		std::size_t operator()(const IKIGAI::Id<T>& k) const {
			using std::size_t;
			using std::hash;
			return (hash<int>()(static_cast<int>(k)));
		}
	};
}
