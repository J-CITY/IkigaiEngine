#include "support/expect.h"
#include "unit/math/math_tests.h"

namespace {
void checkBasics() {
	using namespace IKIGAI::TEST::MATH;
	runVector2();
	runVector3();
	runVector4();
	runAabb();
	runCurves();
}
}

int main(int argc, char** argv) {
	using namespace IKIGAI::TEST::MATH;
	return IKIGAI::TEST::runCases(
		argc, argv, { { "basics", checkBasics }, { "matrix3", runMatrix3 }, { "matrix4", runMatrix4 }, { "quaternion", runQuaternion } });
}
