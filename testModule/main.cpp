#include "support/expect.h"
#include "unit/math/math_tests.h"

#include <iostream>

int main() {
	using namespace IKIGAI::TEST::MATH;
	runVector2();
	runVector3();
	runVector4();
	runMatrix3();
	runMatrix4();
	runQuaternion();
	runAabb();
	runCurves();

	const int failures = IKIGAI::TEST::failureCount();
	if (failures == 0) {
		std::cout << "math_module: all checks passed\n";
		return 0;
	}
	std::cerr << "math_module: " << failures << " check(s) failed\n";
	return 1;
}
