#pragma once

#include <chrono>
#include <string>

#include <ecsModule/weakPtr.h>
#include <utilsModule/result.h>

#include "resourceModule/serializerInterface.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/ref.h"

// because need in all component
#include "utilsModule/reflection/reflection.h"
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>

#include "ecsModule/componentManager.h"

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {

class ComponentBase : public RESOURCES::Serializable, public ECS::Component {
	template <class T> friend UTILS::Result<T, UTILS::JsonError> FromJson(nlohmann::json& data);

public:
	struct Descriptor { };
	// Component();
	ComponentBase(UTILS::Ref<ECS::Object> obj);
	~ComponentBase() override;

	virtual void onAwake() { }
	virtual void onStart() { }

	virtual void onEnable() { }
	virtual void onDisable() { }

	virtual void onDestroy() { }

	virtual void onUpdate(std::chrono::duration<double> dt) { }
	virtual void onFixedUpdate(std::chrono::duration<double> dt) { }
	virtual void onLateUpdate(std::chrono::duration<double> dt) { }

	virtual const std::string& getName() const;
	virtual std::string getTypeidName();

	const ECS::Object& getObject();

	// make it private
	// TODO: use weak_ptr
	UTILS::Ref<ECS::Object> obj;

	virtual void onDeserialize(nlohmann::json& j) override { }
	virtual void onSerialize(nlohmann::json& j) override {
		;
	}

	// template<class T>
	// UTILS::WeakPtr<T> getWeak() {
	//	return UTILS::WeakPtr<T>(*this);
	// }
protected:
	std::string __NAME__;
};

template <class T> inline std::string GetType() {
	return "";
}

template <class T> inline std::string GetComponentName() {
	return "";
}
}
