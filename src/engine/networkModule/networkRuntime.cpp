#include "networkRuntime.h"
#include "networkAsioInternal.h"
#include <deque>
#include <mutex>
#include <algorithm>
#ifdef IKIGAI_NETWORK_ASIO
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/connect.hpp>

namespace IKIGAI::NETWORK {
using namespace DETAIL;
using Tcp = asio::ip::tcp;
using Udp = asio::ip::udp;

void AsioState::stop() {
	{
		std::lock_guard lock(submissionMutex);
		if (!stopping.exchange(true)) {
			// Enqueue shutdown before releasing the guard so the I/O thread can
			// abort pending operations and drain their completion handlers.
			asio::post(io, [this] {
				auto operations = std::move(activeOperations);
				for (const auto& [_, weak] : operations) {
					if (auto watch = weak.lock()) {
						watch->interrupt();
					}
				}
			});
			work.reset();
		}
	}
	{
		std::lock_guard lock(controlsMutex);
		for (const auto& weak : controls) {
			if (auto control = weak.lock()) {
				control->cancel();
			}
		}
		controls.clear();
	}
	if (ioThread == std::this_thread::get_id()) {
		return;
	}
	std::lock_guard lock(joinMutex);
	if (worker.joinable()) {
		worker.join();
	}
}

void AsioState::post(std::function<void()> fn) {
	std::lock_guard lock(submissionMutex);
	if (!stopping) {
		asio::post(io, std::move(fn));
	}
}

AsioState::~AsioState() {
	stop();
}

Watch::Watch(std::shared_ptr<AsioState> state, std::shared_ptr<TransferControl> control, Timeout timeout, std::function<void()> cancel)
	: runtime(std::move(state))
	, control(std::move(control))
	, deadline(std::chrono::steady_clock::now() + timeout)
	, timer(runtime->io, deadline)
	, cancel(std::move(cancel)) { }

void Watch::arm() {
	const auto weak = weak_from_this();
	runtime->activeOperations.emplace(this, weak);
	cancellationCallback = std::make_shared<const std::function<void()>>([weak, weakRuntime = std::weak_ptr(runtime)] {
		if (auto state = weakRuntime.lock()) {
			state->post([weak] {
				if (auto pending = weak.lock()) {
					pending->interrupt();
				}
			});
		}
	});
	control->subscribeToCancellation(cancellationCallback);
	timer.async_wait([weak](boost::system::error_code error) {
		if (!error) {
			if (auto watch = weak.lock()) {
				watch->interrupt();
			}
		}
	});
}

void Watch::interrupt() {
	if (finished || interrupted) {
		return;
	}
	interrupted = true;
	// Propagate cancellation into the coroutine even if an I/O completion
	// is already queued and the coroutine is about to start its next phase.
	signal.emit(asio::cancellation_type::terminal);
	cancel();
}

void Watch::finish() {
	finished = true;
	runtime->activeOperations.erase(this);
	cancellationCallback.reset();
	timer.cancel();
}

void Watch::check() const {
	if (control->isCancelled() || runtime->stopping) {
		throw NetworkError(NetworkErrorCode::cancelled, "Network operation cancelled");
	}
	if (std::chrono::steady_clock::now() >= deadline) {
		throw NetworkError(NetworkErrorCode::timeout, "Network operation timed out");
	}
}

namespace {
	template<class E> Endpoint Address(const E& endpoint) { return {endpoint.address().to_string(), endpoint.port()}; }
	struct Busy {
		bool& flag;
		explicit Busy(bool& value) : flag(value) {
			if (flag) throw NetworkError(NetworkErrorCode::invalidArgument, "An operation is already pending in this direction");
			flag = true;
		}
		~Busy() { flag = false; }
	};
	void CheckSize(size_t size, size_t limit) {
		if (!size || size > limit) throw NetworkError(NetworkErrorCode::invalidArgument, "Network buffer size is out of range");
	}
}
struct TcpStream::Impl {
	std::shared_ptr<AsioState> runtime;
	Tcp::socket socket;
	bool reading = false, writing = false;
	explicit Impl(std::shared_ptr<AsioState> state) : runtime(std::move(state)), socket(runtime->io) { }
	void close() { boost::system::error_code ignored; socket.close(ignored); }
};
struct TcpListener::Impl {
	std::shared_ptr<AsioState> runtime;
	Tcp::acceptor acceptor;
	Endpoint local;
	bool accepting = false;
	explicit Impl(std::shared_ptr<AsioState> state) : runtime(std::move(state)), acceptor(runtime->io) { }
	void close() { boost::system::error_code ignored; acceptor.close(ignored); }
};
struct UdpSocket::Impl {
	std::shared_ptr<AsioState> runtime;
	Udp::socket socket;
	Endpoint local;
	bool reading = false, writing = false;
	explicit Impl(std::shared_ptr<AsioState> state) : runtime(std::move(state)), socket(runtime->io) { }
	void close() { boost::system::error_code ignored; socket.close(ignored); }
};
NetworkRuntime::NetworkRuntime() : mState(std::make_shared<AsioState>()) { }
NetworkRuntime::~NetworkRuntime() {
	if (mState->ioThread == std::this_thread::get_id()) {
		// A transport callback can release the last runtime owner. Keep io_context
		// alive until its thread has returned, then join it from another thread.
		std::thread([state = mState] { state->stop(); }).detach();
	} else stop();
}
NetworkCapabilities NetworkRuntime::capabilities() const {
	return {true, true, true, true,
#ifdef IKIGAI_NETWORK_TLS
		true
#else
		false
#endif
	};
}
Operation<std::shared_ptr<TcpStream>> NetworkRuntime::connect(std::string host, uint16_t port, Timeout timeout) {
	auto stream = std::make_shared<TcpStream::Impl>(mState);
	auto resolver = std::make_shared<Tcp::resolver>(mState->io);
	return Start<std::shared_ptr<TcpStream>>(mState, timeout, [stream, resolver, host = std::move(host), port]() -> asio::awaitable<std::shared_ptr<TcpStream>> {
		auto endpoints = co_await resolver->async_resolve(host, std::to_string(port), asio::use_awaitable);
		co_await asio::async_connect(stream->socket, endpoints, asio::use_awaitable);
		co_return std::shared_ptr<TcpStream>(new TcpStream(stream));
	}, [stream, resolver] { resolver->cancel(); stream->close(); });
}
Operation<std::shared_ptr<TcpListener>> NetworkRuntime::listen(Endpoint endpoint) {
	auto listener = std::make_shared<TcpListener::Impl>(mState);
	return Start<std::shared_ptr<TcpListener>>(mState, DefaultTimeout, [listener, endpoint = std::move(endpoint)]() -> asio::awaitable<std::shared_ptr<TcpListener>> {
		Tcp::endpoint address(asio::ip::make_address(endpoint.address), endpoint.port);
		listener->acceptor.open(address.protocol()); listener->acceptor.set_option(Tcp::acceptor::reuse_address(true));
		listener->acceptor.bind(address); listener->acceptor.listen(16);
		listener->local = Address(listener->acceptor.local_endpoint());
		co_return std::shared_ptr<TcpListener>(new TcpListener(listener));
	}, [listener] { listener->close(); });
}
Operation<std::shared_ptr<UdpSocket>> NetworkRuntime::bindUdp(Endpoint endpoint) {
	auto socket = std::make_shared<UdpSocket::Impl>(mState);
	return Start<std::shared_ptr<UdpSocket>>(mState, DefaultTimeout, [socket, endpoint = std::move(endpoint)]() -> asio::awaitable<std::shared_ptr<UdpSocket>> {
		Udp::endpoint address(asio::ip::make_address(endpoint.address), endpoint.port);
		socket->socket.open(address.protocol()); socket->socket.bind(address); socket->local = Address(socket->socket.local_endpoint());
		co_return std::shared_ptr<UdpSocket>(new UdpSocket(socket));
	}, [socket] { socket->close(); });
}
Operation<std::vector<Endpoint>> NetworkRuntime::resolve(std::string host, uint16_t port, Timeout timeout) {
	auto resolver = std::make_shared<Tcp::resolver>(mState->io);
	return Start<std::vector<Endpoint>>(mState, timeout, [resolver, host = std::move(host), port]() -> asio::awaitable<std::vector<Endpoint>> {
		auto endpoints = co_await resolver->async_resolve(host, std::to_string(port), asio::use_awaitable);
		std::vector<Endpoint> result;
		for (const auto& entry : endpoints) result.push_back(Address(entry.endpoint()));
		co_return result;
	}, [resolver] { resolver->cancel(); });
}
Operation<void> NetworkRuntime::delay(Timeout duration) {
	auto timer = std::make_shared<asio::steady_timer>(mState->io);
	return Start<void>(mState, duration + DefaultTimeout, [timer, duration]() -> asio::awaitable<void> {
		timer->expires_after(duration); co_await timer->async_wait(asio::use_awaitable);
	}, [timer] { timer->cancel(); });
}
TcpStream::TcpStream(std::shared_ptr<Impl> impl) : mImpl(std::move(impl)) { }
TcpStream::~TcpStream() { close(); }
Operation<std::vector<uint8_t>> TcpStream::readSome(size_t limit, Timeout timeout) {
	auto state = mImpl;
	return Start<std::vector<uint8_t>>(state->runtime, timeout, [state, limit]() -> asio::awaitable<std::vector<uint8_t>> {
		CheckSize(limit, 1024 * 1024); Busy busy(state->reading);
		std::vector<uint8_t> bytes(limit);
		boost::system::error_code error;
		const auto size = co_await state->socket.async_read_some(asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
		if (error && error != asio::error::eof) throw boost::system::system_error(error);
		bytes.resize(size); co_return bytes;
	}, [state] { state->close(); });
}
Operation<size_t> TcpStream::writeAll(std::vector<uint8_t> bytes, Timeout timeout) {
	auto state = mImpl;
	return Start<size_t>(state->runtime, timeout, [state, bytes = std::move(bytes)]() -> asio::awaitable<size_t> {
		if (bytes.size() > 1024 * 1024) throw NetworkError(NetworkErrorCode::invalidArgument, "Send buffer exceeds 1 MiB");
		Busy busy(state->writing);
		co_return co_await asio::async_write(state->socket, asio::buffer(bytes), asio::use_awaitable);
	}, [state] { state->close(); });
}
void TcpStream::shutdownSend() {
	auto state = mImpl;
	state->runtime->post([state] { boost::system::error_code ignored; state->socket.shutdown(Tcp::socket::shutdown_send, ignored); });
}
void TcpStream::close() { auto state = mImpl; state->runtime->post([state] { state->close(); }); }
TcpListener::TcpListener(std::shared_ptr<Impl> impl) : mImpl(std::move(impl)) { }
TcpListener::~TcpListener() { close(); }
Endpoint TcpListener::localEndpoint() const { return mImpl->local; }
Operation<std::shared_ptr<TcpStream>> TcpListener::accept(Timeout timeout) {
	auto state = mImpl;
	return Start<std::shared_ptr<TcpStream>>(state->runtime, timeout, [state]() -> asio::awaitable<std::shared_ptr<TcpStream>> {
		Busy busy(state->accepting);
		auto stream = std::make_shared<TcpStream::Impl>(state->runtime);
		co_await state->acceptor.async_accept(stream->socket, asio::use_awaitable);
		co_return std::shared_ptr<TcpStream>(new TcpStream(stream));
	}, [state] { state->close(); });
}
void TcpListener::close() { auto state = mImpl; state->runtime->post([state] { state->close(); }); }
UdpSocket::UdpSocket(std::shared_ptr<Impl> impl) : mImpl(std::move(impl)) { }
UdpSocket::~UdpSocket() { close(); }
Endpoint UdpSocket::localEndpoint() const { return mImpl->local; }
Operation<size_t> UdpSocket::sendTo(Endpoint destination, std::vector<uint8_t> bytes, Timeout timeout) {
	auto state = mImpl;
	return Start<size_t>(state->runtime, timeout, [state, destination = std::move(destination), bytes = std::move(bytes)]() -> asio::awaitable<size_t> {
		if (bytes.size() > 65507) throw NetworkError(NetworkErrorCode::invalidArgument, "UDP datagram exceeds 65507 bytes");
		Busy busy(state->writing);
		co_return co_await state->socket.async_send_to(asio::buffer(bytes), Udp::endpoint(asio::ip::make_address(destination.address), destination.port), asio::use_awaitable);
	}, [state] { state->close(); });
}
Operation<Datagram> UdpSocket::receive(size_t limit, Timeout timeout) {
	auto state = mImpl;
	return Start<Datagram>(state->runtime, timeout, [state, limit]() -> asio::awaitable<Datagram> {
		CheckSize(limit, 65536); Busy busy(state->reading);
		// Read a full datagram even when the caller's limit is smaller: never silently truncate.
		std::vector<uint8_t> bytes(65536); Udp::endpoint sender;
		const auto size = co_await state->socket.async_receive_from(asio::buffer(bytes), sender, asio::use_awaitable);
		if (size > limit) throw NetworkError(NetworkErrorCode::invalidArgument, "Received datagram exceeds buffer limit");
		bytes.resize(size); co_return Datagram{Address(sender), std::move(bytes)};
	}, [state] { state->close(); });
}
void UdpSocket::close() { auto state = mImpl; state->runtime->post([state] { state->close(); }); }
void NetworkRuntime::dispatch(std::function<void()> completion) {
	std::lock_guard lock(mState->completionsMutex);
	if (mState->completions.size() >= 1024) throw NetworkError(NetworkErrorCode::io, "Network completion queue is full");
	mState->completions.push_back(std::move(completion));
}
size_t NetworkRuntime::poll(size_t limit) {
	size_t count = 0;
	for (; count < limit; ++count) {
		std::function<void()> completion;
		{ std::lock_guard lock(mState->completionsMutex); if (mState->completions.empty()) break;
			completion = std::move(mState->completions.front()); mState->completions.pop_front(); }
		completion();
	}
	return count;
}
void NetworkRuntime::stop() { mState->stop(); }
void NetworkRuntime::trackOperation(const std::shared_ptr<TransferControl>& control) {
	std::lock_guard lock(mState->controlsMutex);
	std::erase_if(mState->controls, [](const auto& weak) { return weak.expired(); });
	if (mState->stopping) {
		control->cancel();
	}
	else mState->controls.push_back(control);
}
}
#else
#ifdef IKIGAI_NETWORK_WEB
#include <emscripten.h>
#endif
namespace IKIGAI::NETWORK {
namespace DETAIL { struct AsioState {
	std::mutex mutex;
	std::deque<std::function<void()>> queue;
	std::vector<std::weak_ptr<TransferControl>> controls;
	bool stopped = false;
}; }
namespace {
	template<class T> Operation<T> Unavailable() {
		std::promise<T> promise;
		promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::unsupported, "Transport is unavailable on this platform")));
		return {promise.get_future().share(), std::make_shared<TransferControl>()};
	}
}
struct TcpStream::Impl {};
struct TcpListener::Impl {};
struct UdpSocket::Impl {};
NetworkRuntime::NetworkRuntime() : mState(std::make_shared<DETAIL::AsioState>()) { }
NetworkRuntime::~NetworkRuntime() { stop(); }
NetworkCapabilities NetworkRuntime::capabilities() const {
#ifdef IKIGAI_NETWORK_WEB
	return {false, false, true, true, true};
#else
	return {};
#endif
}
Operation<std::shared_ptr<TcpStream>> NetworkRuntime::connect(std::string, uint16_t, Timeout) { return Unavailable<std::shared_ptr<TcpStream>>(); }
Operation<std::shared_ptr<TcpListener>> NetworkRuntime::listen(Endpoint) { return Unavailable<std::shared_ptr<TcpListener>>(); }
Operation<std::shared_ptr<UdpSocket>> NetworkRuntime::bindUdp(Endpoint) { return Unavailable<std::shared_ptr<UdpSocket>>(); }
Operation<std::vector<Endpoint>> NetworkRuntime::resolve(std::string, uint16_t, Timeout) { return Unavailable<std::vector<Endpoint>>(); }
Operation<void> NetworkRuntime::delay(Timeout duration) {
#ifdef IKIGAI_NETWORK_WEB
	struct Delay {
		std::promise<void> promise;
		std::shared_ptr<TransferControl> control = std::make_shared<TransferControl>();
		std::chrono::steady_clock::time_point end;
		static void tick(void* pointer) {
			auto* state = static_cast<Delay*>(pointer);
			if (state->control->isCancelled()) {
				state->promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::cancelled, "Timer cancelled"))); delete state;
			}
			else if (std::chrono::steady_clock::now() >= state->end) {
				state->promise.set_value();
				delete state;
			}
			else emscripten_async_call(tick, state, 10);
		}
	};
	auto state = std::make_unique<Delay>(); state->end = std::chrono::steady_clock::now() + duration;
	Operation<void> result{state->promise.get_future().share(), state->control}; trackOperation(state->control);
	emscripten_async_call(Delay::tick, state.release(), 0); return result;
#else
	return Unavailable<void>();
#endif
}
void NetworkRuntime::dispatch(std::function<void()> completion) {
	std::lock_guard lock(mState->mutex);
	if (mState->stopped || mState->queue.size() >= 1024) throw NetworkError(NetworkErrorCode::io, "Network completion queue unavailable");
	mState->queue.push_back(std::move(completion));
}
size_t NetworkRuntime::poll(size_t limit) {
	size_t count = 0;
	for (; count < limit; ++count) {
		std::function<void()> completion;
		{ std::lock_guard lock(mState->mutex); if (mState->queue.empty()) break; completion = std::move(mState->queue.front()); mState->queue.pop_front(); }
		completion();
	} return count;
}
void NetworkRuntime::trackOperation(const std::shared_ptr<TransferControl>& control) {
	std::lock_guard lock(mState->mutex);
	std::erase_if(mState->controls, [](const auto& weak) { return weak.expired(); });
	if (mState->stopped) {
		control->cancel();
	}
	else mState->controls.push_back(control);
}
void NetworkRuntime::stop() {
	std::lock_guard lock(mState->mutex); mState->stopped = true;
	for (const auto& weak : mState->controls) {
		if (const auto control = weak.lock()) {
			control->cancel();
		}
	}
	mState->controls.clear();
}
TcpStream::~TcpStream() = default;
Operation<std::vector<uint8_t>> TcpStream::readSome(size_t, Timeout) { return Unavailable<std::vector<uint8_t>>(); }
Operation<size_t> TcpStream::writeAll(std::vector<uint8_t>, Timeout) { return Unavailable<size_t>(); }
void TcpStream::close() { }
void TcpStream::shutdownSend() { }
TcpListener::~TcpListener() = default;
Endpoint TcpListener::localEndpoint() const { return {}; }
Operation<std::shared_ptr<TcpStream>> TcpListener::accept(Timeout) { return Unavailable<std::shared_ptr<TcpStream>>(); }
void TcpListener::close() { }
UdpSocket::~UdpSocket() = default;
Endpoint UdpSocket::localEndpoint() const { return {}; }
Operation<size_t> UdpSocket::sendTo(Endpoint, std::vector<uint8_t>, Timeout) { return Unavailable<size_t>(); }
Operation<Datagram> UdpSocket::receive(size_t, Timeout) { return Unavailable<Datagram>(); }
void UdpSocket::close() { }
}
#endif
