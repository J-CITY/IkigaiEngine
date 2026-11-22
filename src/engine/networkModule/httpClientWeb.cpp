#include "httpClient.h"
#ifdef IKIGAI_NETWORK_WEB
#include "httpUrl.h"
#include <emscripten.h>
#include <nlohmann/json.hpp>

namespace IKIGAI::NETWORK {
namespace {
struct WebRequest {
	std::shared_ptr<TransferControl> control;
	std::promise<void> promise;
	std::function<void(const HttpResponse&)> headers;
	std::function<void(std::span<const uint8_t>)> body;
	std::exception_ptr error;
};
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int ikigai_http_cancelled(void* pointer) {
	return static_cast<WebRequest*>(pointer)->control->isCancelled();
}
EMSCRIPTEN_KEEPALIVE int ikigai_http_headers(void* pointer, int status, const char* json) {
	auto& request = *static_cast<WebRequest*>(pointer);
	try {
		request.control->check();
		HttpResponse response{status, nlohmann::json::parse(json).get<std::map<std::string, std::string>>()};
		request.headers(response); return 0;
	} catch (...) { request.error = std::current_exception(); return 1; }
}
EMSCRIPTEN_KEEPALIVE int ikigai_http_body(void* pointer, const uint8_t* bytes, size_t size) {
	auto& request = *static_cast<WebRequest*>(pointer);
	try {
		request.control->check(); request.body({bytes, size}); request.control->transferred += size; return 0;
	} catch (...) { request.error = std::current_exception(); return 1; }
}
EMSCRIPTEN_KEEPALIVE void ikigai_http_done(void* pointer, const char* error) {
	std::unique_ptr<WebRequest> request(static_cast<WebRequest*>(pointer));
	if (request->error) request->promise.set_exception(request->error);
	else if (request->control->isCancelled()) {
		request->promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::cancelled, "Transfer cancelled")));
	}
	else if (error) request->promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::io, error)));
	else request->promise.set_value();
}
void ikigai_web_http(void*, const char*, const char*, int, double);
}
namespace {
class WebHttpClient final : public IHttpClient {
	std::shared_ptr<NetworkRuntime> mRuntime;
public:
	explicit WebHttpClient(std::shared_ptr<NetworkRuntime> runtime) : mRuntime(runtime ? std::move(runtime) : std::make_shared<NetworkRuntime>()) { }
	std::shared_future<void> getAsync(HttpRequest request, std::shared_ptr<TransferControl> control,
		std::function<void(const HttpResponse&)> headers, std::function<void(std::span<const uint8_t>)> body) override {
		auto work = std::make_unique<WebRequest>();
		auto result = work->promise.get_future().share();
		try {
			DETAIL::HttpUrl url(request);
			if (!control || !headers || !body || request.timeoutSeconds <= 0) throw NetworkError(NetworkErrorCode::invalidArgument, "Invalid HTTP request");
			work->control = std::move(control); work->headers = std::move(headers); work->body = std::move(body);
			mRuntime->trackOperation(work->control);
			work->control->check();
			const auto encoded = nlohmann::json(request.headers).dump();
			ikigai_web_http(work.get(), request.url.c_str(), encoded.c_str(), request.timeoutSeconds, static_cast<double>(request.maxBodyBytes));
			work.release();
		} catch (...) { work->promise.set_exception(std::current_exception()); }
		return result;
	}
};
}
std::unique_ptr<IHttpClient> CreateHttpClient(std::shared_ptr<NetworkRuntime> runtime) { return std::make_unique<WebHttpClient>(std::move(runtime)); }
}
#endif
