#include "support/expect.h"

namespace IKIGAI::TEST {
int runCases(int argc, char** argv, std::initializer_list<Case> cases) {
	if (argc > 2) {
		std::cerr << "Expected at most one test case name\n";
		return 2;
	}
	const std::string_view filter = argc == 2 ? argv[1] : "";
	bool matched = false;
	for (const auto& test : cases) {
		if (argc == 2 && test.name != filter) {
			continue;
		}
		matched = true;
		try {
			test.run();
		} catch (const RequirementFailed&) {
		} catch (const std::exception& error) {
			std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
			++failureCount();
		} catch (...) {
			std::cerr << "FAIL: " << test.name << ": unknown exception\n";
			++failureCount();
		}
	}
	if (!matched) {
		std::cerr << "Unknown test case: " << filter << '\n';
		return 2;
	}
	return failureCount() == 0 ? 0 : 1;
}
}
