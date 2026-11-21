#include "resourceModule/fileSystem/fileSystem.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/serviceManager.h"
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <spine/Extension.h>
#include <spine/SpineString.h>
#include <string>

namespace spine {
namespace {
	class VfsSpineExtension final : public DefaultSpineExtension {
	protected:
		char* _readFile(const String& path, int* length) override {
			*length = 0;
			if (path.isEmpty()) {
				return nullptr;
			}

			const std::string virtualPath(path.buffer(), path.length());
			try {
				auto& files = IKIGAI::UTILS::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();
				const auto file = files.getFile(virtualPath);
				if (!file->isOpened()) {
					LOG_ERROR << "Cannot open Spine file: " << virtualPath;
					return nullptr;
				}
				const auto size = file->getSize();
				if (size == 0 || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
					LOG_ERROR << "Invalid Spine file size: " << virtualPath;
					return nullptr;
				}
				const auto bytes = file->read();
				if (bytes.size() != size) {
					LOG_ERROR << "Short Spine file read: " << virtualPath;
					return nullptr;
				}

				// Spine frees this buffer through its extension and parses JSON as a C string.
				auto* buffer = static_cast<char*>(_alloc(size + 1, __FILE__, __LINE__));
				if (!buffer) {
					return nullptr;
				}
				std::memcpy(buffer, bytes.data(), size);
				buffer[size] = '\0';
				*length = static_cast<int>(size);
				return buffer;
			} catch (const std::exception& error) {
				LOG_ERROR << "Cannot read Spine file " << virtualPath << ": " << error.what();
				return nullptr;
			}
		}
	};
}

SpineExtension* getDefaultExtension() {
	return new VfsSpineExtension();
}
}
