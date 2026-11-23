#pragma once

#include <cmath>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <string_view>

namespace IKIGAI::TEST {

inline int& failureCount() {
	static int count = 0;
	return count;
}

inline void expect(bool condition, const char* name) {
	if (!condition) {
		std::cerr << "FAIL: " << name << '\n';
		++failureCount();
	}
}

struct RequirementFailed { };

inline void require(bool condition, const char* name) {
	if (!condition) {
		expect(false, name);
		throw RequirementFailed {};
	}
}

inline bool near(float actual, float expected, float eps) {
	return std::fabs(actual - expected) <= eps;
}

inline void expectNear(float actual, float expected, float eps, const char* name) {
	if (!near(actual, expected, eps)) {
		std::cerr << "FAIL: " << name << " actual=" << actual << " expected=" << expected << " eps=" << eps << '\n';
		++failureCount();
	}
}

inline void expectNear(float actual, float expected, const char* name) {
	expectNear(actual, expected, 1e-4f, name);
}

template <typename Exception, typename Fn> inline void expectThrows(Fn&& fn, const char* name) {
	try {
		fn();
	} catch (const Exception&) {
		return;
	} catch (...) {
		std::cerr << "Unexpected exception type in " << name << '\n';
	}
	expect(false, name);
}

struct Case {
	std::string_view name;
	void (*run)();
};

int runCases(int argc, char** argv, std::initializer_list<Case> cases);

} // namespace IKIGAI::TEST
