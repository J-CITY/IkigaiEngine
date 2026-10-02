#pragma once

#include <chrono>
#include <coroutine>

namespace IKIGAI::TIME {
	template<typename T>
	struct Generator {
		struct promise_type;
		using handle_type = std::coroutine_handle<promise_type>;

		struct promise_type {
			T value_;
			std::exception_ptr exception_;

			Generator get_return_object() {
				return Generator(handle_type::from_promise(*this));
			}
			std::suspend_always initial_suspend() { return {}; }
			std::suspend_always final_suspend() noexcept { return {}; }
			void unhandled_exception() { exception_ = std::current_exception(); }

			template<std::convertible_to<T> From>
			std::suspend_always yield_value(From&& from) {
				value_ = std::forward<From>(from);
				return {};
			}
			void return_void() {}
		};

		handle_type h_;

		Generator(handle_type h) : h_(h) {}
		~Generator() { h_.destroy(); }
		explicit operator bool() {
			fill();
			return !h_.done();
		}
		T operator()() {
			fill();
			full_ = false;
			return std::move(h_.promise().value_);
		}

	private:
		bool full_ = false;

		void fill() {
			if (!full_) {
				h_();
				if (h_.promise().exception_)
					std::rethrow_exception(h_.promise().exception_);
				full_ = true;
			}
		}
	};

	class Timer {
	public:
		Timer(Timer const&) = delete;
		Timer& operator=(Timer const&) = delete;

		void update();
		[[nodiscard]] double getFPS() const;
		[[nodiscard]] std::chrono::duration<double> getDeltaTime() const;
		[[nodiscard]] std::chrono::duration<double> getDeltaTimeUnscaled() const;
		[[nodiscard]] std::chrono::duration<double> getTimeSinceStart() const;
		[[nodiscard]] double getTimeScale() const;
		void setScale(double s);

		using TimerGenerator = Generator<std::chrono::duration<double>>;

		static Timer& GetInstance();
	private:
		Timer();
		void init();

		TimerGenerator timerGenerator;

		std::chrono::steady_clock::time_point start;
		double scale = 1.0;
		std::chrono::duration<double> dt{};
#ifdef __EMSCRIPTEN__
		double mLastMs = -1.0;
#endif
	};
}
