#include "expression.h"
#include "expressionDescriptor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <stdexcept>

namespace IKIGAI::DATA {
namespace {
	void check(bool ok, const std::string& text) {
		if (!ok) {
			throw std::runtime_error("Expression: " + text);
		}
	}
	bool numeric(ValueType t) {
		return t == ValueType::Float || t == ValueType::Int;
	}
	float finite(float v) {
		check(std::isfinite(v), "non-finite value");
		return v;
	}
}
ValueType typeOf(const Value& v) {
	return static_cast<ValueType>(v.index());
}
Value defaultValue(const std::string& t) {
	if (t == "float") {
		return 0.f;
	}
	if (t == "int") {
		return 0;
	}
	if (t == "bool" || t == "trigger" || t == "event") {
		return false;
	}
	if (t == "Vector3") {
		return MATH::Vector3f {};
	}
	throw std::runtime_error("Unknown value type: " + t);
}
float number(const Value& v) {
	if (auto p = std::get_if<float>(&v)) {
		return finite(*p);
	}
	if (auto p = std::get_if<int>(&v)) {
		return float(*p);
	}
	throw std::runtime_error("Expected numeric value");
}
bool boolean(const Value& v) {
	check(std::holds_alternative<bool>(v), "expected Bool");
	return std::get<bool>(v);
}
Value readValue(const nlohmann::json& j, ValueType t) {
	switch (t) {
	case ValueType::Float:
		check(j.is_number(), "expected Float");
		return finite(j.get<float>());
	case ValueType::Int:
		check(j.is_number_integer() && j.get<int64_t>() >= std::numeric_limits<int>::min() && j.get<int64_t>() <= std::numeric_limits<int>::max(),
			"expected Int");
		return j.get<int>();
	case ValueType::Bool:
		check(j.is_boolean(), "expected Bool");
		return j.get<bool>();
	case ValueType::Vector3:
		return MATH::Vector3f { finite(j.at("X")), finite(j.at("Y")), finite(j.at("Z")) };
	case ValueType::EntityRef:
		throw std::runtime_error("EntityRef requires a live entity binding");
	}
	throw std::runtime_error("Invalid value type");
}
Expression Expression::compile(const nlohmann::json& source, const Symbols& symbols) {
	using J = nlohmann::json;
	static const std::map<std::string, std::tuple<Op, size_t, size_t>> ops = { { "and", { Op::And, 2, 32 } }, { "or", { Op::Or, 2, 32 } },
		{ "not", { Op::Not, 1, 1 } }, { "==", { Op::Equal, 2, 2 } }, { "!=", { Op::NotEqual, 2, 2 } }, { "<", { Op::Less, 2, 2 } },
		{ "<=", { Op::LessEqual, 2, 2 } }, { ">", { Op::Greater, 2, 2 } }, { ">=", { Op::GreaterEqual, 2, 2 } }, { "+", { Op::Add, 2, 2 } },
		{ "-", { Op::Subtract, 2, 2 } }, { "*", { Op::Multiply, 2, 2 } }, { "/", { Op::Divide, 2, 2 } }, { "abs", { Op::Abs, 1, 1 } },
		{ "min", { Op::Min, 2, 2 } }, { "max", { Op::Max, 2, 2 } }, { "clamp", { Op::Clamp, 3, 3 } }, { "if", { Op::Select, 3, 3 } },
		{ "length", { Op::Length, 1, 1 } }, { "horizontalLength", { Op::HorizontalLength, 1, 1 } }, { "dot", { Op::Dot, 2, 2 } },
		{ "x", { Op::X, 1, 1 } }, { "y", { Op::Y, 1, 1 } }, { "z", { Op::Z, 1, 1 } } };
	std::function<ExpressionDescriptor(const J&, int)> decode = [&](const J& value, int depth) -> ExpressionDescriptor {
		check(depth < 64, "nesting exceeds 64");
		if (value.is_object()) {
			auto copy = value;
			return serde::deserialize<ExpressionDescriptor>(copy);
		}
		ExpressionDescriptor descriptor;
		if (value.is_string()) {
			descriptor.Operation = Op::Variable;
			descriptor.Variable = value.get<std::string>();
		}
		else if (value.is_primitive()) {
			descriptor.Constant.Type = value.is_boolean() ? ValueType::Bool : value.is_number_integer() ? ValueType::Int : ValueType::Float;
			auto literal = readValue(value, descriptor.Constant.Type);
			if (auto v = std::get_if<float>(&literal)) {
				descriptor.Constant.Float = *v;
			}
			else if (auto v = std::get_if<int>(&literal)) {
				descriptor.Constant.Int = *v;
			}
			else {
				descriptor.Constant.Bool = std::get<bool>(literal);
			}
		}
		else {
			check(value.is_array() && !value.empty() && value[0].is_string(), "expected operation tree");
			auto operation = ops.find(value[0].get<std::string>());
			check(operation != ops.end(), "unknown legacy operation");
			descriptor.Operation = std::get<0>(operation->second);
			for (size_t i = 1; i < value.size(); ++i) {
				descriptor.Arguments.push_back(decode(value[i], depth + 1));
			}
		}
		return descriptor;
	};
	return compile(decode(source, 0), symbols);
}
Value LiteralDescriptor::value() const {
	switch (Type) {
	case ValueType::Float:
		return finite(Float);
	case ValueType::Int:
		return Int;
	case ValueType::Bool:
		return Bool;
	case ValueType::Vector3:
		return MATH::Vector3f { finite(Vector3.x), finite(Vector3.y), finite(Vector3.z) };
	case ValueType::EntityRef:
		throw std::runtime_error("EntityRef cannot be a numeric literal");
	}
	throw std::runtime_error("Invalid literal type");
}
Expression Expression::compile(const ExpressionDescriptor& source, const Symbols& symbols) {
	Expression result;
	std::function<size_t(const ExpressionDescriptor&, int)> build = [&](const ExpressionDescriptor& e, int depth) -> size_t {
		check(depth < 64, "nesting exceeds 64");
		Node n { Op::Constant, ValueType::Float };
		if (e.Operation == Op::Variable) {
			auto it = symbols.find(e.Variable);
			check(it != symbols.end(), "unknown variable " + e.Variable);
			n.op = Op::Variable;
			n.slot = it->second.slot;
			n.type = it->second.type;
			check(n.type != ValueType::EntityRef, "EntityRef is not supported by numeric expressions: " + e.Variable);
		}
		else if (e.Operation == Op::Constant) {
			n.type = e.Constant.Type;
			n.value = e.Constant.value();
		}
		else {
			const auto op = e.Operation;
			size_t low = 2, high = 2;
			switch (op) {
			case Op::And:
			case Op::Or:
				high = 32;
				break;
			case Op::Not:
			case Op::Abs:
			case Op::Length:
			case Op::HorizontalLength:
			case Op::X:
			case Op::Y:
			case Op::Z:
				low = high = 1;
				break;
			case Op::Clamp:
			case Op::Select:
				low = high = 3;
				break;
			default:
				break;
			}
			check(e.Arguments.size() >= low && e.Arguments.size() <= high, "invalid argument count");
			n.op = op;
			for (const auto& argument : e.Arguments) {
				n.args.push_back(build(argument, depth + 1));
			}
			auto t = [&](size_t i) { return result.nodes[n.args.at(i)].type; };
			if (op == Op::And || op == Op::Or || op == Op::Not) {
				for (auto a : n.args) {
					check(result.nodes[a].type == ValueType::Bool, "logical operation requires Bool");
				}
				n.type = ValueType::Bool;
			}
			else if (op == Op::Equal || op == Op::NotEqual) {
				check(t(0) == t(1) || (numeric(t(0)) && numeric(t(1))), "incompatible equality operands");
				check(t(0) != ValueType::Vector3, "vector equality needs explicit tolerance");
				n.type = ValueType::Bool;
			}
			else if (op == Op::Select) {
				check(t(0) == ValueType::Bool, "if requires Bool");
				check(t(1) == t(2) || (numeric(t(1)) && numeric(t(2))), "incompatible branches");
				n.type = t(1) == t(2) ? t(1) : ValueType::Float;
			}
			else if (op == Op::Length || op == Op::HorizontalLength || op == Op::Dot || op == Op::X || op == Op::Y || op == Op::Z) {
				for (auto a : n.args) {
					check(result.nodes[a].type == ValueType::Vector3, "vector operation requires Vector3");
				}
				n.type = ValueType::Float;
			}
			else {
				for (auto a : n.args) {
					check(numeric(result.nodes[a].type), "numeric operation requires number");
				}
				n.type = (op >= Op::Less && op <= Op::GreaterEqual) ? ValueType::Bool : ValueType::Float;
			}
		}
		result.nodes.push_back(std::move(n));
		return result.nodes.size() - 1;
	};
	result.root = build(source, 0);
	return result;
}
Value Expression::run(size_t i, const std::function<Value(size_t)>& read) const {
	const auto& n = nodes[i];
	auto a = [&](size_t j) { return run(n.args[j], read); };
	auto f = [&](size_t j) { return number(a(j)); };
	switch (n.op) {
	case Op::Constant:
		return n.value;
	case Op::Variable: {
		auto v = read(n.slot);
		check(typeOf(v) == n.type, "runtime variable type changed");
		return v;
	}
	case Op::And:
		for (auto j : n.args) {
			if (!boolean(run(j, read))) {
				return false;
			}
		}
		return true;
	case Op::Or:
		for (auto j : n.args) {
			if (boolean(run(j, read))) {
				return true;
			}
		}
		return false;
	case Op::Not:
		return !boolean(a(0));
	case Op::Select: {
		auto v = a(boolean(a(0)) ? 1 : 2);
		return n.type == ValueType::Float ? Value(number(v)) : v;
	}
	case Op::Equal:
	case Op::NotEqual: {
		auto x = a(0), y = a(1);
		const bool equal = typeOf(x) == ValueType::Bool
			? boolean(x) == boolean(y)
			: (typeOf(x) == ValueType::Int && typeOf(y) == ValueType::Int ? std::get<int>(x) == std::get<int>(y) : number(x) == number(y));
		return n.op == Op::Equal ? equal : !equal;
	}
	case Op::Less:
	case Op::LessEqual:
	case Op::Greater:
	case Op::GreaterEqual: {
		auto x = a(0), y = a(1);
		const double left = typeOf(x) == ValueType::Int ? double(std::get<int>(x)) : double(number(x));
		const double right = typeOf(y) == ValueType::Int ? double(std::get<int>(y)) : double(number(y));
		if (n.op == Op::Less) {
			return left < right;
		}
		if (n.op == Op::LessEqual) {
			return left <= right;
		}
		return n.op == Op::Greater ? left > right : left >= right;
	}
	case Op::Add:
		return finite(f(0) + f(1));
	case Op::Subtract:
		return finite(f(0) - f(1));
	case Op::Multiply:
		return finite(f(0) * f(1));
	case Op::Divide: {
		const float x = f(0), y = f(1);
		return std::abs(y) > .00001f ? finite(x / y) : 0.f;
	}
	case Op::Abs:
		return std::abs(f(0));
	case Op::Min:
		return std::min(f(0), f(1));
	case Op::Max:
		return std::max(f(0), f(1));
	case Op::Clamp:
		return std::max(f(1), std::min(f(2), f(0)));
	case Op::Length: {
		auto v = std::get<MATH::Vector3f>(a(0));
		return finite(std::sqrt(v.dot(v)));
	}
	case Op::HorizontalLength: {
		auto v = std::get<MATH::Vector3f>(a(0));
		return finite(std::sqrt(v.x * v.x + v.z * v.z));
	}
	case Op::Dot:
		return finite(std::get<MATH::Vector3f>(a(0)).dot(std::get<MATH::Vector3f>(a(1))));
	case Op::X:
		return std::get<MATH::Vector3f>(a(0)).x;
	case Op::Y:
		return std::get<MATH::Vector3f>(a(0)).y;
	case Op::Z:
		return std::get<MATH::Vector3f>(a(0)).z;
	}
	throw std::runtime_error("Invalid expression opcode");
}
std::vector<size_t> Expression::dependencies() const {
	std::vector<size_t> r;
	for (const auto& n : nodes) {
		if (n.op == Op::Variable && std::find(r.begin(), r.end(), n.slot) == r.end()) {
			r.push_back(n.slot);
		}
	}
	return r;
}
}
