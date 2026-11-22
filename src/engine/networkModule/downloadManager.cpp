#include "downloadManager.h"
#include "networkRuntime.h"
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <queue>
#include <regex>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace IKIGAI::NETWORK {
namespace {
	struct DownloadWork {
		DownloadRequest request;
		std::shared_ptr<TransferControl> control = std::make_shared<TransferControl>();
		std::promise<std::filesystem::path> promise;
		std::filesystem::path partial, metadata;
		std::ofstream output;
		uint64_t offset = 0;
		bool receivedHeaders = false;
		unsigned attempt = 0;
		std::shared_future<void> commit() const {
			std::promise<void> promise;
			try {
				if (request.commit) {
					auto result = request.commit();
					if (!result.valid())
						throw std::runtime_error("Storage commit returned no completion");
					return result;
				}
				promise.set_value();
			} catch (...) {
				promise.set_exception(std::current_exception());
			}
			return promise.get_future().share();
		}
		bool prepare() {
			control->check();
			receivedHeaders = false;
			output = std::ofstream { };
			if (std::filesystem::exists(request.destination) && std::filesystem::file_size(request.destination) == request.size
				&& request.verify(request.destination)) {
				control->transferred = request.size;
				return false;
			}
			std::filesystem::create_directories(request.destination.parent_path());
			partial = request.destination;
			partial += ".part";
			metadata = partial;
			metadata += ".meta";
			const auto identity = request.http.url + "\n" + request.identity + "\n" + std::to_string(request.size);
			std::string previous;
			{
				std::ifstream input(metadata, std::ios::binary);
				previous.assign(std::istreambuf_iterator<char>(input), { });
			}
			offset = previous == identity && std::filesystem::is_regular_file(partial) ? std::filesystem::file_size(partial) : 0;
			if (offset > request.size)
				offset = 0;
			if (offset == request.size && std::filesystem::exists(partial) && request.verify(partial)) {
				publish();
				return false;
			}
			if (offset == request.size)
				offset = 0;
			if (!offset) {
				std::ofstream clear(partial, std::ios::binary | std::ios::trunc);
				if (!clear)
					throw std::runtime_error("Cannot create download file");
			}
			// A torn sidecar causes a full restart, never acceptance of corrupt data.
			{
				std::ofstream sidecar(metadata, std::ios::binary | std::ios::trunc);
				sidecar.exceptions(std::ios::badbit | std::ios::failbit);
				sidecar << identity;
				sidecar.close();
			}
			control->transferred = offset;
			// A previous retry may have left a stale Range header.
			request.http.headers.erase("Range");
			if (offset)
				request.http.headers["Range"] = "bytes=" + std::to_string(offset) + "-";
			request.http.headers["Accept-Encoding"] = "identity";
			request.http.maxBodyBytes = request.size;
			return true;
		}
		void headers(const HttpResponse& response) {
			control->check();
			if (response.status != 200 && response.status != 206)
				throw std::runtime_error("HTTP status " + std::to_string(response.status));
			if (auto it = response.headers.find("content-encoding"); it != response.headers.end() && !it->second.empty() && it->second != "identity")
				throw std::runtime_error("Encoded archive response");
			if (response.status == 206) {
				const auto found = response.headers.find("content-range");
				if (found == response.headers.end())
					throw std::runtime_error("Missing Content-Range");
				std::smatch match;
				const std::regex range(R"(bytes ([0-9]+)-([0-9]+)/([0-9]+))");
				if (!std::regex_match(found->second, match, range) || std::stoull(match[1]) != offset || std::stoull(match[3]) != request.size
					|| std::stoull(match[2]) == UINT64_MAX || std::stoull(match[2]) + 1 != request.size)
					throw std::runtime_error("Invalid Content-Range");
			}
			else {
				offset = 0;
				control->transferred = 0;
			}
			output.open(partial, std::ios::binary | (offset ? std::ios::app : std::ios::trunc));
			output.exceptions(std::ios::badbit | std::ios::failbit);
			receivedHeaders = true;
		}
		void body(std::span<const uint8_t> bytes) {
			control->check();
			if (!receivedHeaders || offset > request.size || bytes.size() > request.size - offset)
				throw std::runtime_error("Download exceeds declared size");
			output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			offset += bytes.size();
		}
		void publish() {
			if (std::filesystem::exists(request.destination))
				std::filesystem::remove(request.destination);
			std::filesystem::rename(partial, request.destination);
			std::filesystem::remove(metadata);
			control->transferred = request.size;
		}
		void finish() {
			output.close();
			control->check();
			if (!receivedHeaders || offset != request.size || !request.verify(partial))
				throw std::runtime_error("Downloaded artifact failed integrity verification");
			publish();
		}
	};
#ifdef __EMSCRIPTEN__
	void Later(std::function<void()> fn, int milliseconds = 0) {
		auto* owned = new std::function<void()>(std::move(fn));
		emscripten_async_call(
			[](void* pointer) {
				std::unique_ptr<std::function<void()>> callback(static_cast<std::function<void()>*>(pointer));
				(*callback)();
			},
			owned, milliseconds);
	}
	void Await(std::shared_future<void> future, std::function<void(std::exception_ptr)> done) {
		if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
			Later([future, done = std::move(done)] { Await(future, done); }, 5);
			return;
		}
		std::exception_ptr error;
		try {
			future.get();
		} catch (...) {
			error = std::current_exception();
		}
		done(error);
	}
#endif
}
struct DownloadManager::State : std::enable_shared_from_this<State> {
	std::unique_ptr<IHttpClient> client;
	std::mutex mutex;
	std::condition_variable wake;
	std::queue<std::shared_ptr<DownloadWork>> queue;
	std::shared_ptr<DownloadWork> active;
	bool stopping = false;
#ifndef __EMSCRIPTEN__
	std::thread worker;
#endif
	explicit State(std::unique_ptr<IHttpClient> client)
		: client(std::move(client)) {
		if (!this->client)
			throw std::runtime_error("Missing HTTP client");
	}
	void stop() {
		{
			std::lock_guard lock(mutex);
			stopping = true;
			if (active)
				active->control->cancel();
			while (!queue.empty()) {
				auto work = queue.front();
				queue.pop();
				work->control->cancel();
				work->promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::cancelled, "Download manager stopped")));
			}
		}
		wake.notify_all();
#ifndef __EMSCRIPTEN__
		if (worker.joinable())
			worker.join();
#endif
	}
#ifndef __EMSCRIPTEN__
	void run() {
		for (;;) {
			std::shared_ptr<DownloadWork> work;
			{
				std::unique_lock lock(mutex);
				wake.wait(lock, [this] { return stopping || !queue.empty(); });
				if (stopping)
					return;
				work = queue.front();
				queue.pop();
				active = work;
			}
			try {
				for (;;) {
					try {
						if (work->prepare()) {
							// Network operations are asynchronous on Asio; this bounded worker
							// waits for the stream, then performs file verification/publication.
							client
								->getAsync(
									work->request.http, work->control, [work](const auto& h) { work->headers(h); },
									[work](auto bytes) { work->body(bytes); })
								.get();
							work->finish();
						}
						work->commit().get();
						work->control->check();
						work->promise.set_value(work->request.destination);
						break;
					} catch (...) {
						work->output = std::ofstream { };
						if (work->control->isCancelled() || work->attempt >= std::min(work->request.retries, 5u)) {
							throw;
						}
						std::unique_lock lock(mutex);
						wake.wait_for(lock, std::chrono::milliseconds(250u << work->attempt++),
							[this, &work] { return stopping || work->control->isCancelled(); });
					}
				}
			} catch (...) {
				work->promise.set_exception(std::current_exception());
			}
			{
				std::lock_guard lock(mutex);
				active.reset();
			}
		}
	}
#else
	void pump() {
		std::shared_ptr<DownloadWork> work;
		{
			std::lock_guard lock(mutex);
			if (stopping || active || queue.empty())
				return;
			work = queue.front();
			queue.pop();
			active = work;
		}
		attempt(work);
	}
	void attempt(const std::shared_ptr<DownloadWork>& work) {
		try {
			if (!work->prepare()) {
				commit(work);
				return;
			}
			auto pending = client->getAsync(
				work->request.http, work->control, [work](const auto& h) { work->headers(h); }, [work](auto bytes) { work->body(bytes); });
			Await(pending, [self = shared_from_this(), work](std::exception_ptr error) {
				if (error) {
					self->failed(work, error);
					return;
				}
				try {
					work->finish();
					self->commit(work);
				} catch (...) {
					self->failed(work, std::current_exception());
				}
			});
		} catch (...) {
			failed(work, std::current_exception());
		}
	}
	void commit(const std::shared_ptr<DownloadWork>& work) {
		Await(work->commit(), [self = shared_from_this(), work](std::exception_ptr error) {
			if (error)
				work->promise.set_exception(error);
			else if (work->control->isCancelled()) {
				work->promise.set_exception(std::make_exception_ptr(NetworkError(NetworkErrorCode::cancelled, "Download cancelled")));
			}
			else
				work->promise.set_value(work->request.destination);
			self->next();
		});
	}
	void failed(const std::shared_ptr<DownloadWork>& work, std::exception_ptr error) {
		work->output = std::ofstream { };
		Await(work->commit(), [self = shared_from_this(), work, error](std::exception_ptr commitError) {
			if (!commitError && !work->control->isCancelled() && work->attempt < std::min(work->request.retries, 5u)) {
				Later([self, work] { self->attempt(work); }, 250u << work->attempt++);
				return;
			}
			work->promise.set_exception(commitError ? commitError : error);
			self->next();
		});
	}
	void next() {
		{
			std::lock_guard lock(mutex);
			active.reset();
		}
		Later([self = shared_from_this()] { self->pump(); });
	}
#endif
};
DownloadManager::DownloadManager(std::unique_ptr<IHttpClient> client)
	: mState(std::make_shared<State>(std::move(client))) {
#ifndef __EMSCRIPTEN__
	mState->worker = std::thread([state = mState] { state->run(); });
#endif
}
DownloadManager::~DownloadManager() {
	mState->stop();
}
DownloadHandle DownloadManager::download(DownloadRequest request) {
	if (request.identity.empty() || request.identity.find_first_of("\r\n") != std::string::npos || !request.verify || request.destination.empty())
		throw std::runtime_error("Invalid download request");
	auto work = std::make_shared<DownloadWork>();
	work->request = std::move(request);
	DownloadHandle result { work->control, work->promise.get_future().share() };
	{
		std::lock_guard lock(mState->mutex);
		if (mState->stopping || mState->queue.size() >= 64)
			throw std::runtime_error("Download queue unavailable");
		mState->queue.push(work);
	}
#ifdef __EMSCRIPTEN__
	Later([state = mState] { state->pump(); });
#else
	mState->wake.notify_one();
#endif
	return result;
}
}
