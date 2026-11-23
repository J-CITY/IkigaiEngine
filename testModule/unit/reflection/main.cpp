#include "support/expect.h"

#include "reflectionModule/reflection.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using IKIGAI::UTILS::Any;
using IKIGAI::UTILS::any_cast;
using IKIGAI::UTILS::Metadata;
using IKIGAI::UTILS::ReflectionManager;

struct Probe {
	int value = 1;
	int add(int amount) {
		value += amount;
		return value;
	}
};

struct MoveOnly {
	std::unique_ptr<int> value;
};

void checkBasics() {
	using IKIGAI::UTILS::string_hash;

	constexpr auto hashed = string_hash("module");
	expect(hashed == string_hash(std::string_view("module")), "reflection.hash.view");
	expect(hashed != string_hash("modules"), "reflection.hash.differs");
	expect(hashed == string_hash(std::string("module").data(), 6), "reflection.hash.length");
	expect(ReflectionManager::GetTypeId<int>() != ReflectionManager::GetTypeId<float>(), "reflection.typeId.differs");

	Any value(42);
	expect(any_cast<int>(value) == 42, "reflection.any.int");
	Any copy = value;
	expect(any_cast<int>(copy) == 42, "reflection.any.copy");
	expect(any_cast<float>(&value) == nullptr, "reflection.any.wrongType");
	expectThrows<std::bad_cast>([&] { (void)any_cast<float>(value); }, "reflection.any.valueThrows");

	Any held(MoveOnly { std::make_unique<int>(3) });
	expectThrows<std::runtime_error>([&] { Any duplicated = held; }, "reflection.any.moveOnlyCopy");
	Any transferred = std::move(held);
	const auto* moved = any_cast<MoveOnly>(&transferred);
	expect(moved != nullptr && moved->value && *moved->value == 3, "reflection.any.move");

	Metadata metadata;
	metadata.add("hp", Any(3));
	metadata.add("hp", Any(9));
	expect(metadata.get<int>("hp") == 9, "reflection.metadata.replace");
	expect(metadata.get<int>("missing", -1) == -1, "reflection.metadata.default");
	expect(!metadata.contains("missing") && metadata.contains("hp"), "reflection.metadata.contains");

	auto& reflection = ReflectionManager::Instance();
	reflection.registerProperty<int>("module-test-property");
	expect(reflection.property<int>("module-test-property") == 0, "reflection.property.default");
	reflection.property<int>("module-test-property") = 5;
	expect(reflection.property<int>("module-test-property") == 5, "reflection.property.write");
	reflection.removeProperty("module-test-property");
	expectThrows<std::runtime_error>([&] { (void)reflection.property<int>("module-test-property"); }, "reflection.property.removed");
}

void checkRegistration() {
	auto& reflection = ReflectionManager::Instance();
	reflection.registerType<Probe>();
	Metadata metadata;
	metadata.add("minimum", Any(1));
	reflection.registerField("value", &Probe::value, std::move(metadata));
	reflection.registerMethod("add", &Probe::add);
	reflection.registerType<Probe>();

	const auto* type = reflection.getType<Probe>();
	require(type != nullptr, "reflection.registeredTypeExists");
	expect(reflection.getTypes().contains(type->mId) && reflection.getType("Probe") == type, "reflection.registry.lookupAliases");
	const auto field = type->mFields.find("value");
	const auto method = type->mMethods.find("add");
	require(field != type->mFields.end() && method != type->mMethods.end(), "reflection.reregister.preservesMembers");
	expect(field->second.metadata.get<int>("minimum", -1) == 1, "reflection.reregister.preservesFieldMetadata");
	Probe probe;
	expect(field->second.get<int>(&probe) == 1, "reflection.field.get");
	field->second.set(&probe, 4);
	expect(probe.value == 4, "reflection.field.set");
	const auto added = method->second.invoke(&probe, { Any(3) });
	expect(any_cast<int>(added) == 7 && probe.value == 7, "reflection.method");
	expectThrows<std::runtime_error>([&] { (void)method->second.invoke(&probe, {}); }, "reflection.method.arity");
	expectThrows<std::bad_cast>([&] { (void)method->second.invoke(&probe, { Any(3.f) }); }, "reflection.method.wrongArgumentType");
	expect(probe.value == 7, "reflection.method.rejectionLeavesObjectUnchanged");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "basics", checkBasics }, { "registration", checkRegistration } });
}
