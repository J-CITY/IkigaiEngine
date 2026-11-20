#include "stringUtils.h"

#include <algorithm>
#include <ranges>
#include <regex>

namespace IKIGAI::UTILS {

void ReplaceSubstringsInPlace(std::string& subject, const std::string& search, const std::string& replace) {
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
}

void ReplaceSubstringsInPlaceRegex(std::string& subject, const std::string& search, const std::string& replace) {

	std::regex vowel_re(search);
	subject = std::regex_replace(subject, vowel_re, replace);
}

std::string ReplaceSubstrings(const std::string& _subject, const std::string& search, const std::string& replace) {
	auto subject = _subject;
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
	return subject;
}

std::string ReplaceSubstring(const std::string& _subject, const std::string& search, const std::string& replace) {
	auto subject = _subject;
	size_t pos = 0;
	if (subject.find(search, pos) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
	}
	return subject;
}

std::string ReplaceSubstringsRegex(const std::string& subject, const std::string& search, const std::string& replace) {
	std::regex vowel_re(search);
	return std::regex_replace(subject, vowel_re, replace);
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
	auto to_string = [](auto&& r) {
		const auto data = &*r.begin();
		const auto size = static_cast<std::size_t>(std::ranges::distance(r));
		return std::string { data, size };
	};
	auto range = str | std::ranges::views::split(delimiter) | std::ranges::views::transform(to_string);

	return { std::ranges::begin(range), std::ranges::end(range) };
}

std::string ToLower(std::string_view s) {
	std::string data(s.begin(), s.end());
	std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) { return std::tolower(c); });
	return data;
}
std::string ToUpper(std::string_view s) {
	std::string data(s.begin(), s.end());
	std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) { return std::toupper(c); });
	return data;
}

bool IsFindInString(const std::string& s, const std::string& subs) {
	return s.find(subs) != std::string::npos;
}

std::string NormalizeTextInput(std::string_view input) {
	if (input.starts_with("\xef\xbb\xbf")) {
		input.remove_prefix(3);
	}
	std::string result;
	result.reserve(input.size());
	for (std::size_t index = 0; index < input.size(); ++index) {
		if (input[index] == '\r') {
			result += '\n';
			if (index + 1 < input.size() && input[index + 1] == '\n') {
				++index;
			}
		}
		else {
			result += input[index];
		}
	}
	return result;
}
}
