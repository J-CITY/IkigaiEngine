#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace IKIGAI::NETWORK {
class NetworkRuntime;
namespace DETAIL {
	struct Watch;
}
class TransferControl {
	friend struct DETAIL::Watch;
	std::atomic<bool> mCancelled { false };
	std::mutex mCancellationMutex;
	std::vector<std::weak_ptr<const std::function<void()>>> mCancellationCallbacks;
	void subscribeToCancellation(const std::shared_ptr<const std::function<void()>>& callback);

public:
	std::atomic<uint64_t> transferred { 0 };
	// Cancellation is permanent for this control and wakes pending native I/O.
	void cancel();
	bool isCancelled() const;
	void check() const;
};
struct HttpRequest {
	std::string url;
	std::map<std::string, std::string> headers;
	int timeoutSeconds = 30;
	bool allowInsecureLocalhost = false;
	// Optional PEM trust bundle for platforms without an OpenSSL default CA store.
	std::string caBundle;
	uint64_t maxBodyBytes = UINT64_MAX;
};
struct HttpResponse {
	int status = 0;
	std::map<std::string, std::string> headers;
};
class IHttpClient {
public:
	virtual ~IHttpClient() = default;
	// Streaming handlers apply backpressure and run on the transport executor.
	// Native handlers must not access ECS/UI. Web handlers run in its event loop.
	virtual std::shared_future<void> getAsync(HttpRequest request, std::shared_ptr<TransferControl> control,
		std::function<void(const HttpResponse&)> headers, std::function<void(std::span<const uint8_t>)> body) = 0;
	// Compatibility bridge for existing native worker callers; forbidden in Web.
	void get(const HttpRequest& request, TransferControl& control, const std::function<void(const HttpResponse&)>& headers,
		const std::function<void(std::span<const uint8_t>)>& body);
};
std::unique_ptr<IHttpClient> CreateHttpClient(std::shared_ptr<NetworkRuntime> runtime = { });
}
