#include "jsonError.h"

#include <utility>

namespace IKIGAI::UTILS {
JsonError::JsonError(Kind kind, std::string text, std::string path, std::vector<std::string> inheritanceChain)
	: kind(kind)
	, text(std::move(text))
	, path(std::move(path))
	, inheritanceChain(std::move(inheritanceChain)) {
	if (!this->path.empty()) {
		this->text += " [" + this->path + "]";
	}
	if (this->inheritanceChain.size() > 1) {
		this->text += " (##parent chain: " + this->inheritanceChain.front();
		for (std::size_t index = 1; index < this->inheritanceChain.size(); ++index) {
			this->text += " -> " + this->inheritanceChain[index];
		}
		this->text += ")";
	}
}
}
