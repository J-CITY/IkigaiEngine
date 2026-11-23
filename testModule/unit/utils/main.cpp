#include "support/expect.h"

#include "utilsModule/containers/smallVector.h"
#include "utilsModule/stringUtils.h"

#include <string>

using IKIGAI::TEST::expect;

int main() {
	using IKIGAI::UTILS::NormalizeTextInput;
	using IKIGAI::UTILS::SmallVector;
	using IKIGAI::UTILS::Split;
	using IKIGAI::UTILS::ToLower;

	const auto parts = Split("a,b,c", ',');
	expect(parts.size() == 3 && parts[0] == "a" && parts[2] == "c", "utils.split");
	expect(ToLower("AbC") == "abc", "utils.lower");
	expect(NormalizeTextInput("\xEF\xBB\xBFline\r\nnext\rlast") == "line\nnext\nlast", "utils.normalize");

	SmallVector<int, 2, std::allocator<int>> values;
	values.push_back(1);
	values.push_back(2);
	values.push_back(3);
	expect(values.size() == 3 && values[0] == 1 && values[2] == 3, "utils.smallVector.grows");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
