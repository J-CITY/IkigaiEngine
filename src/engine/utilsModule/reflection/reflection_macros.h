#pragma once

// These macros are empty for the C++ compiler.
// They are parsed by the IkigaiHeaderTool to generate reflection data.

namespace IKIGAI::UTILS {
// Call during startup before querying generated metadata; repeated calls are safe.
void RegisterGeneratedReflection();
template <typename T> struct ReflectionReg;
}

#define IKI_CLASS(...)
#define IKI_PROPERTY(...)
#define IKI_FUNCTION(...)
#define IKI_ENUM(...)

#define IKI_GENERATED_BODY(ClassName)                                                                                                                \
	friend struct IKIGAI::UTILS::ReflectionReg<ClassName>;                                                                                           \
	template <class Context> constexpr static auto serde(Context& context, ClassName& value) {                                                       \
		return ikigai_serde(context, value);                                                                                                         \
	}
