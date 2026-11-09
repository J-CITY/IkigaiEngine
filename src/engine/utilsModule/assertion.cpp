#include "assertion.h"
#include "crashReport.h"

#include <cstdio>

void IKIGAI::UTILS::__Assert(const char* expr_str, bool expr, const char* file, int line, const char* msg) {
	if (!expr) {
		char reason[1024] {};
		std::snprintf(reason, sizeof(reason), "Assertion failed at %s:%d\nExpected: %s\n%s", file, line, expr_str, msg);
		CRASH::terminate(reason);
	}
}
