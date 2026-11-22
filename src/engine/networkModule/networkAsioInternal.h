#pragma once
// Private backend header; Boost types never appear in the public engine API.
#include "networkRuntime.h"
#ifdef IKIGAI_NETWORK_ASIO
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#ifdef IKIGAI_NETWORK_TLS
#include <boost/asio/ssl/context.hpp>
#endif

namespace IKIGAI::NETWORK {
namespace DETAIL {
	namespace asio = boost::asio;
#ifdef IKIGAI_NETWORK_TLS
	void ConfigureTrust(asio::ssl::context& context, const std::string& bundle);
#endif
	struct Watch;
	struct AsioState {
		asio::io_context io;
		asio::executor_work_guard<asio::io_context::executor_type> work { io.get_executor() };
		std::atomic<bool> stopping { false };
		std::mutex submissionMutex;
		std::mutex joinMutex;
		std::thread worker { [this] { io.run(); } };
		const std::thread::id ioThread = worker.get_id();
		std::mutex controlsMutex;
		std::vector<std::weak_ptr<TransferControl>> controls;
		std::mutex completionsMutex;
		std::deque<std::function<void()>> completions;
		// Only the I/O thread accesses active operations.
		std::unordered_map<Watch*, std::weak_ptr<Watch>> activeOperations;
		void stop();
		void post(std::function<void()> fn);
		~AsioState();
	};
	struct Watch : std::enable_shared_from_this<Watch> {
		std::shared_ptr<AsioState> runtime;
		std::shared_ptr<TransferControl> control;
		const std::chrono::steady_clock::time_point deadline;
		asio::steady_timer timer;
		std::function<void()> cancel;
		asio::cancellation_signal signal;
		std::shared_ptr<const std::function<void()>> cancellationCallback;
		bool interrupted = false;
		bool finished = false;
		Watch(std::shared_ptr<AsioState> state, std::shared_ptr<TransferControl> control, Timeout timeout, std::function<void()> cancel);
		void arm();
		void interrupt();
		void finish();
		void check() const;
	};
	template <class T> asio::awaitable<T> Run(std::shared_ptr<Watch> watch, std::function<asio::awaitable<T>()> job) {
		try {
			watch->check();
			watch->arm();
			watch->check();
			if constexpr (std::is_void_v<T>) {
				co_await job();
				watch->check();
				watch->finish();
				co_return;
			}
			else {
				auto result = co_await job();
				watch->check();
				watch->finish();
				co_return result;
			}
		} catch (const boost::system::system_error& error) {
			watch->finish();
			watch->check();
			throw NetworkError(NetworkErrorCode::io, error.what());
		} catch (...) {
			watch->finish();
			throw;
		}
	}
	template <class T, class F>
	Operation<T> Start(
		const std::shared_ptr<AsioState>& state, Timeout timeout, F job, std::function<void()> cancel = [] { },
		std::shared_ptr<TransferControl> control = std::make_shared<TransferControl>()) {
		auto promise = std::make_shared<std::promise<T>>();
		Operation<T> result { promise->get_future().share(), control };
		std::lock_guard submission(state->submissionMutex);
		if (state->stopping) {
			promise->set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::cancelled, "Network runtime stopped")));
			return result;
		}
		auto watch = std::make_shared<Watch>(state, control, timeout, std::move(cancel));
		if constexpr (std::is_void_v<T>) {
			asio::co_spawn(state->io, Run<T>(watch, std::function<asio::awaitable<T>()>(std::move(job))),
				asio::bind_cancellation_slot(watch->signal.slot(), [promise, watch](std::exception_ptr error) {
					if (error) {
						promise->set_exception(error);
					}
					else {
						promise->set_value();
					}
				}));
		}
		else {
			asio::co_spawn(state->io, Run<T>(watch, std::function<asio::awaitable<T>()>(std::move(job))),
				asio::bind_cancellation_slot(watch->signal.slot(), [promise, watch](std::exception_ptr error, T value) {
					if (error) {
						promise->set_exception(error);
					}
					else {
						promise->set_value(std::move(value));
					}
				}));
		}
		return result;
	}
	template <class T> Operation<T> Unsupported() {
		std::promise<T> promise;
		promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::unsupported, "Transport is unavailable on this platform")));
		return { promise.get_future().share(), std::make_shared<TransferControl>() };
	}
}
struct NativeAccess {
	static std::shared_ptr<DETAIL::AsioState> state(const std::shared_ptr<NetworkRuntime>& runtime) {
		return runtime->mState;
	}
};
}
#endif
