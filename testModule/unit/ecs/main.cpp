#include "support/expect.h"

namespace IKIGAI::TEST::ECS_CASES {
void runStorageTests();
void runSystemTests();
}

int main() {
	IKIGAI::TEST::ECS_CASES::runStorageTests();
	IKIGAI::TEST::ECS_CASES::runSystemTests();
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
