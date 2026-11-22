#pragma once
#include "httpClient.h"
#include <filesystem>
#include <future>

namespace IKIGAI::NETWORK {
struct DownloadRequest {
	HttpRequest http;
	std::filesystem::path destination;
	uint64_t size = 0;
	std::string identity;
	std::function<bool(const std::filesystem::path&)> verify;
	// Optional persistence barrier supplied by the destination storage owner.
	// Keeps the network module independent of profiles/SDL/IndexedDB.
	std::function<std::shared_future<void>()> commit;
	unsigned retries = 2;
};
struct DownloadHandle {
	std::shared_ptr<TransferControl> control;
	std::shared_future<std::filesystem::path> result;
	void cancel() const {
		if (control)
			control->cancel();
	}
};
// Native: one file/verification worker, with network I/O on Asio's executor.
// Web: cooperative event-loop progress, no pthread or blocking future wait.
class DownloadManager {
	struct State;
	std::shared_ptr<State> mState;

public:
	explicit DownloadManager(std::unique_ptr<IHttpClient> client = CreateHttpClient());
	~DownloadManager();
	DownloadManager(const DownloadManager&) = delete;
	DownloadManager& operator=(const DownloadManager&) = delete;
	DownloadHandle download(DownloadRequest request);
};
}
