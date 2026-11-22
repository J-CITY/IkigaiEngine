#include "httpClient.h"
#include "networkRuntime.h"
namespace IKIGAI::NETWORK {
void TransferControl::cancel() {
	decltype(mCancellationCallbacks) callbacks;
	{
		std::lock_guard lock(mCancellationMutex);
		if (mCancelled.exchange(true)) {
			return;
		}
		callbacks.swap(mCancellationCallbacks);
	}
	for (const auto& weak : callbacks) {
		if (const auto callback = weak.lock()) {
			(*callback)();
		}
	}
}

bool TransferControl::isCancelled() const {
	return mCancelled.load();
}

void TransferControl::subscribeToCancellation(const std::shared_ptr<const std::function<void()>>& callback) {
	std::lock_guard lock(mCancellationMutex);
	if (mCancelled) {
		return;
	}
	std::erase_if(mCancellationCallbacks, [](const auto& weak) { return weak.expired(); });
	mCancellationCallbacks.push_back(callback);
}

void TransferControl::check() const {
	if (isCancelled()) {
		throw NetworkError(NetworkErrorCode::cancelled, "Transfer cancelled");
	}
}
void IHttpClient::get(const HttpRequest& request, TransferControl& control, const std::function<void(const HttpResponse&)>& headers,
	const std::function<void(std::span<const uint8_t>)>& body) {
#ifdef __EMSCRIPTEN__
	throw NetworkError(NetworkErrorCode::unsupported, "Blocking HTTP is unavailable in the browser; use getAsync");
#else
	getAsync(request, std::shared_ptr<TransferControl>(&control, [](auto*) { }), headers, body).get();
#endif
}
#if !defined(IKIGAI_NETWORK_ASIO) && !defined(IKIGAI_NETWORK_WEB)
namespace {
	class OfflineHttpClient final : public IHttpClient {
		std::shared_future<void> getAsync(HttpRequest, std::shared_ptr<TransferControl>, std::function<void(const HttpResponse&)>,
			std::function<void(std::span<const uint8_t>)>) override {
			std::promise<void> promise;
			promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::unsupported, "HTTP backend is disabled")));
			return promise.get_future().share();
		}
	};
}
std::unique_ptr<IHttpClient> CreateHttpClient(std::shared_ptr<NetworkRuntime>) {
	return std::make_unique<OfflineHttpClient>();
}
#endif
}
