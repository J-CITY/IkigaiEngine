#include "support/expect.h"

#include "utilsModule/containers/smallVector.h"
#include "utilsModule/expressions/expression.h"
#include "utilsModule/expressions/expressionDescriptor.h"
#include "utilsModule/serialization/jsonAdapters.h"
#include "utilsModule/stringUtils.h"

#include <stdexcept>
#include <string>
#include <vector>

using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

namespace {
using IKIGAI::DATA::Expression;
using IKIGAI::DATA::ExpressionDescriptor;
using IKIGAI::DATA::Op;
using IKIGAI::DATA::Symbols;
using IKIGAI::DATA::Value;
using IKIGAI::DATA::ValueType;
using IKIGAI::UTILS::SmallVector;

IKIGAI::DATA::ExpressionDescriptor constant(float number) {
	IKIGAI::DATA::ExpressionDescriptor descriptor;
	descriptor.Operation = IKIGAI::DATA::Op::Constant;
	descriptor.Constant.Type = IKIGAI::DATA::ValueType::Float;
	descriptor.Constant.Float = number;
	return descriptor;
}

IKIGAI::DATA::ExpressionDescriptor constantBool(bool value) {
	IKIGAI::DATA::ExpressionDescriptor descriptor;
	descriptor.Operation = IKIGAI::DATA::Op::Constant;
	descriptor.Constant.Type = IKIGAI::DATA::ValueType::Bool;
	descriptor.Constant.Bool = value;
	return descriptor;
}

void checkBasics() {
	using IKIGAI::UTILS::IsFindInString;
	using IKIGAI::UTILS::NormalizeTextInput;
	using IKIGAI::UTILS::ReplaceSubstring;
	using IKIGAI::UTILS::ReplaceSubstrings;
	using IKIGAI::UTILS::ReplaceSubstringsInPlace;
	using IKIGAI::UTILS::ReplaceSubstringsRegex;
	using IKIGAI::UTILS::Split;
	using IKIGAI::UTILS::ToLower;
	using IKIGAI::UTILS::ToUpper;

	const auto parts = Split("a,b,c", ',');
	expect(parts.size() == 3 && parts[0] == "a" && parts[2] == "c", "utils.split");
	expect(Split("", ',').empty(), "utils.split.empty");
	const auto blanks = Split("a,,b", ',');
	expect(blanks.size() == 3 && blanks[1].empty(), "utils.split.blank");
	expect(ToLower("AbC") == "abc" && ToUpper("AbC") == "ABC", "utils.case");
	expect(IsFindInString("scaffold", "fold") && !IsFindInString("scaffold", "xxx"), "utils.find");
	expect(ReplaceSubstring("ababa", "aba", "Z") == "Zba", "utils.replace.first");
	std::string repeated = "aaa";
	ReplaceSubstringsInPlace(repeated, "aa", "b");
	expect(repeated == "ba" && ReplaceSubstrings("a-a", "a", "xx") == "xx-xx", "utils.replace.all");
	expect(ReplaceSubstringsRegex("a1b2", "[0-9]", "#") == "a#b#", "utils.replace.regex");
	expect(NormalizeTextInput("\xEF\xBB\xBFline\r\nnext\rlast") == "line\nnext\nlast", "utils.normalize");
	expect(NormalizeTextInput("plain") == "plain", "utils.normalize.plain");

	SmallVector<int, 2, std::allocator<int>> values;
	values.push_back(1);
	values.push_back(2);
	expect(values.is_inline() && values.size() == 2, "utils.smallVector.inline");
	values.push_back(3);
	expect(!values.is_inline() && values[0] == 1 && values[2] == 3, "utils.smallVector.grows");
	auto duplicate = values;
	expect(duplicate.size() == 3 && duplicate[1] == 2, "utils.smallVector.copy");
	values.pop_back();
	expect(values.size() == 2 && values.back() == 2, "utils.smallVector.pop");
	values.clear();
	expect(values.empty() && !values.is_inline(), "utils.smallVector.clearKeepsHeap");
	values.shrink_to_fit();
	expect(values.is_inline(), "utils.smallVector.shrink");
	values = { 4, 5 };
	expect(values.size() == 2 && values.front() == 4, "utils.smallVector.assign");

	IKIGAI::DATA::ExpressionDescriptor sum;
	sum.Operation = Op::Add;
	sum.Arguments = { constant(2.f), constant(3.f) };
	const auto addition = Expression::compile(sum, {});
	const auto added = addition.evaluate([](std::size_t) { return Value { 0.f }; });
	expect(addition.type() == IKIGAI::DATA::ValueType::Float && std::get<float>(added) == 5.f, "utils.expression.add");

	IKIGAI::DATA::Symbols symbols { { "x", { 0, IKIGAI::DATA::ValueType::Float } } };
	IKIGAI::DATA::ExpressionDescriptor variable;
	variable.Operation = Op::Variable;
	variable.Variable = "x";
	const auto read = Expression::compile(variable, symbols).evaluate([](std::size_t) { return Value { 4.f }; });
	expect(std::get<float>(read) == 4.f, "utils.expression.variable");
}

class CopyFailure : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

struct ElementLifetime {
	int live = 0;
	int rejectValue = -1;
};

struct TrackedElement {
	ElementLifetime& lifetime;
	int value;

	TrackedElement(ElementLifetime& state, int initialValue)
		: lifetime(state)
		, value(initialValue) {
		++lifetime.live;
	}
	TrackedElement(const TrackedElement& other)
		: lifetime(other.lifetime)
		, value(other.value) {
		if (value == lifetime.rejectValue) {
			throw CopyFailure("injected element copy failure");
		}
		++lifetime.live;
	}
	TrackedElement(TrackedElement&& other) noexcept(false)
		: TrackedElement(other.lifetime, other.value) {
		other.value = -1;
	}
	~TrackedElement() {
		--lifetime.live;
	}
};

void checkSmallVectorLifetime() {
	ElementLifetime lifetime;
	{
		SmallVector<TrackedElement, 2, std::allocator<TrackedElement>> elements;
		elements.emplace_back(lifetime, 1);
		elements.emplace_back(lifetime, 2);
		lifetime.rejectValue = 2;
		expectThrows<CopyFailure>([&] { elements.emplace_back(lifetime, 3); }, "utils.smallVector.growthPropagatesElementCopyFailure");
		require(elements.size() == 2 && elements.is_inline(), "utils.smallVector.failedGrowthKeepsStorage");
		expect(lifetime.live == 2 && elements[0].value == 1 && elements[1].value == 2,
			"utils.smallVector.failedGrowthPreservesElementsAndReleasesCopies");
		lifetime.rejectValue = -1;
		elements.emplace_back(lifetime, 3);
		require(elements.size() == 3 && !elements.is_inline(), "utils.smallVector.retryGrows");
		expect(lifetime.live == 3 && elements[0].value == 1 && elements[1].value == 2 && elements[2].value == 3,
			"utils.smallVector.retryPreservesElements");
		elements.pop_back();
		expect(lifetime.live == 2, "utils.smallVector.popDestroysElement");
	}
	expect(lifetime.live == 0, "utils.smallVector.scopeDestroysRemainingElements");
}

void checkExpressionCompilation() {
	ExpressionDescriptor unused;
	unused.Operation = Op::Variable;
	unused.Variable = "unused";
	ExpressionDescriptor selected = unused;
	selected.Variable = "selected";

	ExpressionDescriptor choice;
	choice.Operation = Op::Select;
	choice.Arguments = { constantBool(false), unused, selected };
	auto document = serde::serialize<nlohmann::json>(choice);
	const auto loaded = serde::deserialize<ExpressionDescriptor>(document);
	const Symbols symbols { { "unused", { 3, ValueType::Float } }, { "selected", { 7, ValueType::Float } } };
	std::vector<std::size_t> reads;
	const auto result = Expression::compile(loaded, symbols).evaluate([&](std::size_t id) {
		reads.push_back(id);
		return Value { id == 7 ? 9.f : 1.f };
	});
	expect(std::get_if<float>(&result) && std::get<float>(result) == 9.f, "utils.expression.selectReturnsChosenValue");
	expect(reads == std::vector<std::size_t> { 7 }, "utils.expression.selectReadsOnlyChosenBranch");

	expectThrows<std::runtime_error>([&] { (void)Expression::compile(loaded, {}); }, "utils.expression.rejectsUnknownSymbol");
	ExpressionDescriptor invalid = loaded;
	invalid.Arguments.pop_back();
	expectThrows<std::runtime_error>([&] { (void)Expression::compile(invalid, symbols); }, "utils.expression.rejectsWrongArity");
	invalid = loaded;
	invalid.Arguments[0] = constant(1.f);
	expectThrows<std::runtime_error>([&] { (void)Expression::compile(invalid, symbols); }, "utils.expression.rejectsNonBooleanCondition");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv,
		{ { "basics", checkBasics }, { "small_vector_lifetime", checkSmallVectorLifetime },
		{ "expression_compilation", checkExpressionCompilation } });
}
