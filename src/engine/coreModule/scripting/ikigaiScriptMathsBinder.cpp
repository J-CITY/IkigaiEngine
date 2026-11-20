#if USE_IKIGAI_SCRIPT
#include "ikigaiScriptBinder.h"
#include <Library/native/native_class_builder.hpp>
namespace IKIGAI::SCRIPTING {
using namespace IkigaiScript;
using namespace IKIGAI::MATH;
void IkigaiScriptBinder::function(const std::string& scope, const std::string& name, Lambda fn) {
	auto* module = vm.findModuleByName(scope);
	auto s = module ? module->scope : vm.newModule(scope, 0, { });
	vm.newFunction(name, s, fn);
}
template <class T, int N> static void vectorType(IkigaiScriptBinder& b, IkigaiScriptInterpreter& vm, const char* name) {
	b.mathName<T>(name);
	Native::NativeClassBuilder<T> builder(vm, name);
	builder.member("x", &T::x).member("y", &T::y).template constructor<>();
	if constexpr (N == 2)
		builder.template constructor<float>().template constructor<float, float>();
	if constexpr (N >= 3)
		builder.member("z", &T::z);
	if constexpr (N == 3)
		builder.template constructor<float>().template constructor<float, float, float>();
	if constexpr (N == 4)
		builder.member("w", &T::w).template constructor<float, float, float, float>();
	if constexpr (N < 4) {
		builder.operator_("+", [](const T& a, const T& c) { return a + c; });
		builder.operator_("-", [](const T& a, const T& c) { return a - c; });
		builder.operator_("*", [](T a, float s) { return a * s; });
		builder.operator_("/", [](T a, float s) { return a / s; });
	}
	builder.finalize();
}
template <class T, int N> static void matrixType(IkigaiScriptBinder& b, IkigaiScriptInterpreter& vm, const char* name) {
	b.mathName<T>(name);
	vm.newClass(name, { },
		[&b, &vm](Class* c, ScopePtr, const List& a) -> ValuePtr {
			T v;
			if (a.size() == 1)
				v = T(b.read<float>(a[0]));
			else if (a.size() == N * N)
				for (size_t i = 0; i < N * N; ++i)
					v.getData()[i] = b.read<float>(a[i]);
			else if (!a.empty())
				throw std::runtime_error("Invalid matrix constructor");
			c->variables = b.value(v)->getClass()->variables;
			return vm.makeValue();
		},
		{ });
	b.template method<T>("Get", [&b](T& m, const List& a) {
		auto r = b.read<unsigned>(a.at(0)), c = b.read<unsigned>(a.at(1));
		if (r >= N || c >= N)
			throw std::out_of_range("Matrix index");
		return b.value(m(r, c));
	});
	b.template method<T>("Set", [&b, &vm](T& m, const List& a) {
		auto r = b.read<unsigned>(a.at(0)), c = b.read<unsigned>(a.at(1));
		if (r >= N || c >= N)
			throw std::out_of_range("Matrix index");
		m(r, c) = b.read<float>(a.at(2));
		return vm.makeValue();
	});
	b.freeFunction(name, "IsIdentity", &T::IsIdentity);
	b.freeFunction(name, "Determinant", &T::Determinant);
	b.freeFunction(name, "Transpose", &T::Transpose);
	b.freeFunction(name, "Inverse", &T::Inverse);
	b.template binaryOperator<T, T>("+", [](T a, T c) { return a + c; });
	b.template binaryOperator<T, T>("-", [](T a, T c) { return a - c; });
	b.template binaryOperator<T, T>("*", [](T a, T c) { return a * c; });
	b.template binaryOperator<T, float>("*", [](T a, float c) { return a * c; });
	b.template binaryOperator<T, float>("/", [](T a, float c) { return a / c; });
}
void IkigaiScriptBinder::bindMaths() {
	vectorType<Vector2f, 2>(*this, vm, "Vector2");
	vectorType<Vector3f, 3>(*this, vm, "Vector3");
	vectorType<Vector4f, 4>(*this, vm, "Vector4");
	function("Vector3", "Length", [this](const Args& a) { return value(Vector3f::Length(read<Vector3f>(a.at(0)))); });
	function("Vector3", "Normalize", [this](const Args& a) { return value(Vector3f::Normalize(read<Vector3f>(a.at(0)))); });
	function("Vector3", "Dot", [this](const Args& a) { return value(Vector3f::Dot(read<Vector3f>(a.at(0)), read<Vector3f>(a.at(1)))); });
	function("Vector3", "Cross", [this](const Args& a) { return value(Vector3f::Cross(read<Vector3f>(a.at(0)), read<Vector3f>(a.at(1)))); });
	function("Vector3", "Distance", [this](const Args& a) { return value(Vector3f::Distance(read<Vector3f>(a.at(0)), read<Vector3f>(a.at(1)))); });
	function("Vector3", "Lerp",
		[this](const Args& a) { return value(Vector3f::Lerp(read<Vector3f>(a.at(0)), read<Vector3f>(a.at(1)), read<float>(a.at(2)))); });
	for (auto [name, v] : std::initializer_list<std::pair<const char*, Vector3f>> { { "One", Vector3f::One }, { "Zero", Vector3f::Zero },
		 { "Forward", Vector3f::Forward }, { "Up", Vector3f::Up }, { "Right", Vector3f::Right } })
		function("Vector3", name, [this, v](const Args&) { return value(v); });
	mathName<QuaternionF>("Quaternion");
	vm.newClass("Quaternion", { },
		[this](C* self, ScopePtr, const Args& args) -> V {
			QuaternionF q;
			if (args.size() == 1)
				q = args[0]->getType() == Type::Class ? QuaternionF(read<Vector3f>(args[0])) : QuaternionF(read<float>(args[0]));
			else if (args.size() == 4)
				q = QuaternionF(read<float>(args[0]), read<float>(args[1]), read<float>(args[2]), read<float>(args[3]));
			else if (!args.empty())
				throw std::runtime_error("Quaternion expects 0, 1 or 4 arguments");
			self->variables = value(q)->getClass()->variables;
			return vm.makeValue();
		},
		{ });
	function("Quaternion", "Multiply", [this](const Args& a) { return value(read<QuaternionF>(a.at(0)) * read<QuaternionF>(a.at(1))); });
	function("Quaternion", "ToEulerAngles", [this](const Args& a) {
		return value(QuaternionF::ToEulerAngles(read<QuaternionF>(a.at(0))) * static_cast<float>(180.0 / 3.141592653589793));
	});
	function("Quaternion", "Normalize", [this](const Args& a) { return value(QuaternionF::Normalize(read<QuaternionF>(a.at(0)))); });
	function("Quaternion", "RotatePoint",
		[this](const Args& a) { return value(QuaternionF::RotatePoint(read<Vector3f>(a.at(0)), read<QuaternionF>(a.at(1)))); });
	matrixType<Matrix3f, 3>(*this, vm, "Matrix3");
	matrixType<Matrix4f, 4>(*this, vm, "Matrix4");
	freeFunction("Matrix3", "Cofactor", &Matrix3f::Cofactor);
	freeFunction("Matrix3", "Minor", &Matrix3f::Minor);
	freeFunction("Matrix3", "Adjoint", &Matrix3f::Adjoint);
	freeFunction("Matrix3", "GetRow", &Matrix3f::GetRow);
	freeFunction("Matrix3", "GetColumn", &Matrix3f::GetColumn);
	function("Matrix3", "Rotation", [this](const Args& a) {
		return value(a.size() == 1 ? Matrix3f::Rotation(read<float>(a.at(0))) : Matrix3f::Rotation(read<Matrix3f>(a.at(0)), read<float>(a.at(1))));
	});
	freeFunction("Matrix4", "Minor", &Matrix4f::GetMinor);
	freeFunction("Matrix4", "Translation", &Matrix4f::Translation);
	freeFunction("Matrix4", "CreatePerspective", &Matrix4f::CreatePerspective);
	freeFunction("Matrix4", "CreateFrustum", &Matrix4f::CreateFrustum);
	binaryOperator<Matrix3f, Vector3f>("*", [](Matrix3f m, Vector3f v) { return m * v; });
	binaryOperator<Matrix4f, Vector4f>("*", [](Matrix4f m, Vector4f v) { return m * v; });
	binaryOperator<QuaternionF, QuaternionF>("*", [](QuaternionF a, QuaternionF c) { return a * c; });
	binaryOperator<QuaternionF, Vector3f>("*", [](QuaternionF q, Vector3f v) { return q * v; });
	binaryOperator<QuaternionF, float>("*", [](QuaternionF q, float v) { return q * v; });
	member("Length", &Vector2f::length);
	member("Normalize", &Vector2f::normalize);
	member("Dot", &Vector2f::dot);
	freeFunction("Vector3", "AngleBetween", &Vector3f::AngleBetween);
	freeFunction("Quaternion", "IsNormalized", &QuaternionF::IsNormalized);
	freeFunction("Quaternion", "Length", &QuaternionF::Length);
	freeFunction("Quaternion", "LengthSquare", &QuaternionF::LengthSquare);
	freeFunction("Quaternion", "ToQuaternion", &QuaternionF::ToQuaternion);
	function("Matrix3", "Identity", [this](const Args&) { return value(Matrix3f::Identity); });
	function("Matrix4", "Identity", [this](const Args&) { return value(Matrix4f::Identity); });
	function("Quaternion", "ToMatrix3f", [this](const Args& a) { return value(QuaternionF::ToMatrix3(read<QuaternionF>(a.at(0)))); });
	function("Quaternion", "ToMatrix4f", [this](const Args& a) { return value(QuaternionF::ToMatrix4(read<QuaternionF>(a.at(0)))); });
}

}
#endif
