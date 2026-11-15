#include "math_tests.h"
#include "support/expect.h"

#include "mathModule/geometry/aabb.h"

#include <limits>

using IKIGAI::MATH::GEOMETRY::Aabb;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;

namespace IKIGAI::TEST::MATH {

void runAabb() {
	using Box = Aabb<3>;

	Box a;
	a.min = { 0.f, 0.f, 0.f };
	a.max = { 1.f, 1.f, 1.f };
	Box b;
	b.min = { 0.5f, 0.5f, 0.5f };
	b.max = { 2.f, 2.f, 2.f };
	Box c;
	c.min = { 2.f, 2.f, 2.f };
	c.max = { 3.f, 3.f, 3.f };

	expect(a.overlaps(b), "aabb.overlaps");
	expect(!a.overlaps(c), "aabb.no_overlap");

	const Box merged = Box::merge(a, b);
	expect(merged.min[0] == 0.f && merged.min[1] == 0.f && merged.min[2] == 0.f, "aabb.merge.min");
	expect(merged.max[0] == 2.f && merged.max[1] == 2.f && merged.max[2] == 2.f, "aabb.merge.max");

	expectThrows(
		[] {
			Box invalid;
			invalid.min = { 1.f, 0.f, 0.f };
			invalid.max = { 0.f, 1.f, 1.f };
			invalid.validate();
		},
		"aabb.validate.min_gt_max");

	expectThrows(
		[] {
			Box invalid;
			invalid.min = { std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f };
			invalid.max = { 1.f, 1.f, 1.f };
			invalid.validate();
		},
		"aabb.validate.nan");
}

} // namespace IKIGAI::TEST::MATH
