#include "webSocket.h"
#ifdef IKIGAI_NETWORK_WEB
#include "httpUrl.h"
#include <algorithm>
#include <emscripten.h>
namespace IKIGAI::NETWORK {
namespace {
	struct WebWsWork {
		std::shared_ptr<TransferControl> control;
		std::function<void(int, const char*, const uint8_t*, size_t, bool)> finish;
	};
}
extern "C" {
int ikigai_web_ws_create();
void ikigai_web_ws_destroy(int);
void ikigai_web_ws_operation(int, int, void*, const uint8_t*, size_t, int, int);
EMSCRIPTEN_KEEPALIVE int ikigai_ws_cancelled(void* pointer) {
	return static_cast<WebWsWork*>(pointer)->control->isCancelled();
}
EMSCRIPTEN_KEEPALIVE void ikigai_ws_done(void* pointer, int code, const char* error, const uint8_t* bytes, size_t size, int text) {
	std::unique_ptr<WebWsWork> work(static_cast<WebWsWork*>(pointer));
	work->finish(code, error, bytes, size, text != 0);
}
}
namespace {
	class WebWebSocket final : public IWebSocketClient {
		int mId = ikigai_web_ws_create();
		std::shared_ptr<NetworkRuntime> mRuntime;
		template <class T, class F> Operation<T> start(int operation, const uint8_t* bytes, size_t size, bool text, Timeout timeout, F convert) {
			auto promise = std::make_shared<std::promise<T>>();
			auto control = std::make_shared<TransferControl>();
			mRuntime->trackOperation(control);
			Operation<T> result { promise->get_future().share(), control };
			if (control->isCancelled()) {
				promise->set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::cancelled, "Network runtime stopped")));
				return result;
			}
			auto work = std::make_unique<WebWsWork>();
			work->control = control;
			work->finish = [promise, control, convert](int code, const char* error, const uint8_t* data, size_t count, bool text) {
				try {
					if (code || control->isCancelled()) {
						throw NetworkError(control->isCancelled() ? NetworkErrorCode::cancelled
								: code == 2						  ? NetworkErrorCode::timeout
																  : NetworkErrorCode::io,
							error ? error : "WebSocket operation failed");
					}
					if constexpr (std::is_void_v<T>)
						promise->set_value();
					else
						promise->set_value(convert(data, count, text));
				} catch (...) {
					promise->set_exception(std::current_exception());
				}
			};
			ikigai_web_ws_operation(
				mId, operation, work.release(), bytes, size, text, static_cast<int>(std::clamp<int64_t>(timeout.count(), 1, INT32_MAX)));
			return result;
		}

	public:
		explicit WebWebSocket(std::shared_ptr<NetworkRuntime> runtime)
			: mRuntime(runtime ? std::move(runtime) : std::make_shared<NetworkRuntime>()) { }
		~WebWebSocket() override {
			ikigai_web_ws_destroy(mId);
		}
		Operation<void> connect(HttpRequest request) override {
			try {
				DETAIL::HttpUrl url(request, true);
				if (!request.headers.empty())
					throw NetworkError(NetworkErrorCode::invalidArgument, "Custom WebSocket headers are not portable to browsers");
				return start<void>(0, reinterpret_cast<const uint8_t*>(request.url.c_str()), request.url.size(), false,
					std::chrono::duration_cast<Timeout>(std::chrono::seconds(request.timeoutSeconds)), [](auto, auto, auto) { });
			} catch (...) {
				std::promise<void> promise;
				promise.set_exception(std::current_exception());
				return { promise.get_future().share(), std::make_shared<TransferControl>() };
			}
		}
		Operation<WebSocketMessage> read(Timeout timeout) override {
			return start<WebSocketMessage>(1, nullptr, 0, false, timeout, [](const uint8_t* bytes, size_t size, bool text) {
				WebSocketMessage message;
				if (size)
					message.bytes.assign(bytes, bytes + size);
				message.text = text;
				return message;
			});
		}
		Operation<size_t> write(WebSocketMessage message, Timeout timeout) override {
			return start<size_t>(2, message.bytes.data(), message.bytes.size(), message.text, timeout,
				[size = message.bytes.size()](auto, auto, auto) { return size; });
		}
		Operation<void> close(Timeout timeout) override {
			return start<void>(3, nullptr, 0, false, timeout, [](auto, auto, auto) { });
		}
	};
}
std::unique_ptr<IWebSocketClient> CreateWebSocketClient(std::shared_ptr<NetworkRuntime> runtime) {
	return std::make_unique<WebWebSocket>(std::move(runtime));
}
}
#endif
