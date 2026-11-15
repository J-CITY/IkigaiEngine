#pragma once

#include <cmath>
#include <iostream>
#include <string>

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

inline void expectNear(float actual, float expected, float eps, const char* name) {
	if (!(std::fabs(actual - expected) <= eps)) {
		std::cerr << "FAIL: " << name << " actual=" << actual << " expected=" << expected << " eps=" << eps << '\n';
		++failureCount();
	}
}

inline void expectNear(float actual, float expected, const char* name) {
	expectNear(actual, expected, 1e-4f, name);
}

template <typename Fn> inline void expectThrows(Fn&& fn, const char* name) {
	bool threw = false;
	try {
		fn();
	} catch (...) {
		threw = true;
	}
	expect(threw, name);
}

} // namespace IKIGAI::TEST
