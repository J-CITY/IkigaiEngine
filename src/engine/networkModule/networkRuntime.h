#pragma once
#include "httpClient.h"
#include <chrono>
#include <future>
#include <vector>
#include <stdexcept>

namespace IKIGAI::NETWORK {
namespace DETAIL { struct AsioState; }
enum class NetworkErrorCode { unsupported, cancelled, timeout, invalidArgument, io, tls };
class NetworkError : public std::runtime_error {
public:
	NetworkErrorCode code;
	NetworkError(NetworkErrorCode code, const std::string& message) : std::runtime_error(message), code(code) { }
};
template<class T> struct Operation {
	std::shared_future<T> result;
	std::shared_ptr<TransferControl> control;
	void cancel() const {
		control->cancel();
	}
};
struct Endpoint { std::string address; uint16_t port = 0; };
struct Datagram { Endpoint sender; std::vector<uint8_t> bytes; };
struct NetworkCapabilities { bool tcp = false, udp = false, http = false, websocket = false, tls = false; };
using Timeout = std::chrono::milliseconds;
inline constexpr Timeout DefaultTimeout{30000};
class TcpListener;
class NetworkRuntime;
class TcpStream {
	struct Impl;
	std::shared_ptr<Impl> mImpl;
	explicit TcpStream(std::shared_ptr<Impl> impl);
	friend class NetworkRuntime;
	friend class TcpListener;
public:
	~TcpStream();
	// One outstanding read and one write per stream; returned bytes may be partial.
	Operation<std::vector<uint8_t>> readSome(size_t limit = 65536, Timeout timeout = DefaultTimeout);
	Operation<size_t> writeAll(std::vector<uint8_t> bytes, Timeout timeout = DefaultTimeout);
	void shutdownSend();
	void close();
};
class TcpListener {
	struct Impl;
	std::shared_ptr<Impl> mImpl;
	explicit TcpListener(std::shared_ptr<Impl> impl);
	friend class NetworkRuntime;
public:
	~TcpListener();
	Endpoint localEndpoint() const;
	Operation<std::shared_ptr<TcpStream>> accept(Timeout timeout = DefaultTimeout);
	void close();
};
class UdpSocket {
	struct Impl;
	std::shared_ptr<Impl> mImpl;
	explicit UdpSocket(std::shared_ptr<Impl> impl);
	friend class NetworkRuntime;
public:
	~UdpSocket();
	Endpoint localEndpoint() const;
	Operation<size_t> sendTo(Endpoint destination, std::vector<uint8_t> bytes, Timeout timeout = DefaultTimeout);
	Operation<Datagram> receive(size_t limit = 65536, Timeout timeout = DefaultTimeout);
	void close();
};
class NetworkRuntime {
	std::shared_ptr<DETAIL::AsioState> mState;
	friend struct NativeAccess;
public:
	NetworkRuntime();
	~NetworkRuntime();
	NetworkRuntime(const NetworkRuntime&) = delete;
	NetworkRuntime& operator=(const NetworkRuntime&) = delete;
	NetworkCapabilities capabilities() const;
	Operation<std::shared_ptr<TcpStream>> connect(std::string host, uint16_t port, Timeout timeout = DefaultTimeout);
	Operation<std::shared_ptr<TcpListener>> listen(Endpoint endpoint);
	Operation<std::shared_ptr<UdpSocket>> bindUdp(Endpoint endpoint);
	Operation<std::vector<Endpoint>> resolve(std::string host, uint16_t port, Timeout timeout = DefaultTimeout);
	Operation<void> delay(Timeout duration);
	// Explicit bridge to the engine thread: callbacks run only when poll() is called.
	void dispatch(std::function<void()> completion);
	size_t poll(size_t limit = 64);
	// Backend extension point: bind an operation's cancellation to this runtime.
	void trackOperation(const std::shared_ptr<TransferControl>& control);
	void stop();
};
}
