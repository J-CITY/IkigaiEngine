#include "support/expect.h"

namespace IKIGAI::TEST::ECS_CASES {
void runStorageTests();
void runSystemTests();
}

int main(int argc, char** argv) {
	using namespace IKIGAI::TEST::ECS_CASES;
	return IKIGAI::TEST::runCases(argc, argv, { { "storage", runStorageTests }, { "systems", runSystemTests } });
}
