#pragma once
#include "mathModule/math.h"
#include "utilsModule/variables/entityRef.h"
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <variant>
#include <vector>

namespace IKIGAI::DATA {
enum class ValueType { Float, Int, Bool, Vector3, EntityRef };
using Value = std::variant<float, int, bool, MATH::Vector3f, EntityRef>;
ValueType typeOf(const Value&);
Value defaultValue(const std::string& type);
Value readValue(const nlohmann::json&, ValueType);
float number(const Value&);
bool boolean(const Value&);
enum class Op {
	Constant,
	Variable,
	And,
	Or,
	Not,
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
	Add,
	Subtract,
	Multiply,
	Divide,
	Abs,
	Min,
	Max,
	Clamp,
	Select,
	Length,
	HorizontalLength,
	Dot,
	X,
	Y,
	Z
};
struct Symbol {
	size_t slot;
	ValueType type;
};
using Symbols = std::map<std::string, Symbol>;

// Authoring names and JSON are resolved once; execution only uses opcodes and slots.
struct ExpressionDescriptor;
class Expression {
	struct Node {
		Op op;
		ValueType type;
		Value value = 0.f;
		size_t slot = 0;
		std::vector<size_t> args;
	};
	std::vector<Node> nodes;
	size_t root = 0;
	Value run(size_t, const std::function<Value(size_t)>&) const;

public:
	static Expression compile(const nlohmann::json&, const Symbols&);
	static Expression compile(const ExpressionDescriptor&, const Symbols&);
	Value evaluate(const std::function<Value(size_t)>& read) const {
		return run(root, read);
	}
	ValueType type() const {
		return nodes.at(root).type;
	}
	std::vector<size_t> dependencies() const;
};
}
