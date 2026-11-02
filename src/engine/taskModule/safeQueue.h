#pragma once

#include <condition_variable>
#include <deque>
#include <optional>
#include <shared_mutex>

namespace IKIGAI {
namespace TASK {
	template <typename T> class ThreadSafeQueue {
	public:
		~ThreadSafeQueue() {
			invalidate();
		}

		std::optional<T> tryPop() {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			if (mQueue.empty() || !mValid) {
				return std::nullopt;
			}
			auto out = std::move(mQueue.front());
			mQueue.pop_front();
			return out;
		}

		// Owner pops the front; a thief takes the back so the two ends do not fight over the same item.
		std::optional<T> tryPopBack() {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			if (mQueue.empty() || !mValid) {
				return std::nullopt;
			}
			auto out = std::move(mQueue.back());
			mQueue.pop_back();
			return out;
		}

		std::optional<T> waitPop() {
			std::unique_lock<std::shared_mutex> lock { mMutex };
			mCondition.wait(lock, [this]() { return !mQueue.empty() || !mValid; });

			if (!mValid) {
				return std::nullopt;
			}
			auto out = std::move(mQueue.front());
			mQueue.pop_front();
			return out;
		}

		void push(T& value) {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mQueue.push_back(value);
			mCondition.notify_one();
		}

		void push(T&& value) {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mQueue.push_back(std::move(value));
			mCondition.notify_one();
		}

		void pushFront(const T& value) {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mQueue.push_front(value);
			mCondition.notify_one();
		}

		void pushFront(T&& value) {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mQueue.push_front(std::move(value));
			mCondition.notify_one();
		}

		bool empty() const {
			std::shared_lock<std::shared_mutex> lock { mMutex };
			return mQueue.empty();
		}

		void clear() {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mQueue.clear();
			mCondition.notify_all();
		}

		bool isValid() const {
			std::shared_lock<std::shared_mutex> lock { mMutex };
			return mValid;
		}

		void invalidate() {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mValid = false;
			mCondition.notify_all();
		}

		size_t size() {
			std::shared_lock<std::shared_mutex> lock { mMutex };
			return mQueue.size();
		}

		std::deque<T>& getRawData() {
			std::shared_lock<std::shared_mutex> lock { mMutex };
			return mQueue;
		}

		void setRawData(std::deque<T>&& values) {
			std::lock_guard<std::shared_mutex> lock { mMutex };
			mQueue = values;
		}

	private:
		std::atomic_bool mValid { true };
		mutable std::shared_mutex mMutex;
		std::deque<T> mQueue;
		std::condition_variable_any mCondition;
	};
}
}
