#include "support/expect.h"

#include "reflectionModule/reflection.h"

#include <string>
#include <utility>

using IKIGAI::TEST::expect;

int main() {
	using IKIGAI::UTILS::Any;
	using IKIGAI::UTILS::any_cast;
	using IKIGAI::UTILS::Metadata;
	using IKIGAI::UTILS::string_hash;

	constexpr auto hashed = string_hash("module");
	expect(hashed == string_hash(std::string_view("module")), "reflection.hash.matchesView");
	expect(hashed != string_hash("modules"), "reflection.hash.differs");

	Any value(42);
	expect(any_cast<int>(value) == 42, "reflection.any.int");
	Any copy = value;
	expect(any_cast<int>(copy) == 42, "reflection.any.copy");
	expect(any_cast<int>(&value) != nullptr, "reflection.any.cast");
	expect(any_cast<float>(&value) == nullptr, "reflection.any.wrongType");

	Metadata metadata;
	metadata.add("hp", Any(3));
	expect(metadata.get<int>("hp") == 3, "reflection.metadata.hit");
	expect(metadata.get<int>("missing", -1) == -1, "reflection.metadata.default");
	return IKIGAI::TEST::failureCount() == 0 ? 0 : 1;
}
