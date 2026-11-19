#pragma once

#include <string>
#include <vector>

namespace IKIGAI::UTILS {
struct JsonError {
	enum class Kind { PARSE, FILE_NOT_EXIST, SEREALIZE, INVALID_PATH, FILE_READ, INVALID_PARENT, PARENT_CYCLE, PARENT_DEPTH };

	JsonError() = default;
	JsonError(Kind kind, std::string text, std::string path = {}, std::vector<std::string> inheritanceChain = {});

	Kind kind = Kind::PARSE;
	std::string text;
	std::string path;
	std::vector<std::string> inheritanceChain;
};
}
