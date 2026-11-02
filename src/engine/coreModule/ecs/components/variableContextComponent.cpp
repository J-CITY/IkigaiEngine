#include "variableContextComponent.h"
#include "coreModule/ecs/componentReference.inl"
#include <algorithm>
namespace IKIGAI::ECS {
VariableContextComponent::VariableContextComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VariableContextComponent";
	settings.Type = GetType<VariableContextComponent>();
}
VariableContextComponent::VariableContextComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: VariableContextComponent(object) {
	settings = static_cast<const Descriptor&>(descriptor);
}
size_t VariableContextComponent::slot(const std::string& name) const {
	if (!mRuntime) {
		throw std::runtime_error("Variable context is not prepared");
	}
	return mRuntime->schema().key(name);
}
DATA::ValueType VariableContextComponent::type(size_t key) const {
	return mRuntime->schema().keys.at(key).Type;
}
const DATA::Value& VariableContextComponent::read(size_t key) const {
	const auto& value = mRuntime->readSnapshot(key);
	if (!value) {
		throw std::runtime_error("Read of Unset context snapshot: " + mRuntime->schema().keys.at(key).Name);
	}
	return *value;
}
const std::optional<DATA::Value>& VariableContextComponent::readCurrent(size_t key) const {
	return mRuntime->readCurrent(key);
}
void VariableContextComponent::write(size_t key, const DATA::Value& value) {
	mRuntime->write(key, value);
}
void VariableContextComponent::clear(size_t key) {
	mRuntime->clear(key);
}
void VariableContextComponent::fire(size_t key) {
	mRuntime->fire(key);
}
uint64_t VariableContextComponent::tick() const {
	return mRuntime ? mRuntime->tick() : 0;
}
std::shared_ptr<DATA::VariableContext> VariableContextComponent::runtime() const {
	return mRuntime;
}
void VariableContextComponent::prepare(SCENE_SYSTEM::Scene& scene) {
	DATA::VariableSchemaDescriptor descriptor;
	if (!settings.Schema.empty()) {
		auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(settings.Schema);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Missing variable context schema: " + settings.Schema);
		}
		descriptor = DATA::readDescriptor<DATA::VariableSchemaDescriptor>(nlohmann::json::parse(file->readStr()));
	}
	for (const auto& [name, variable] : settings.Variables) {
		const int sources = int(variable.Property.has_value()) + int(variable.Expression.has_value()) + int(variable.Script.has_value())
			+ int(variable.ObjectId.has_value());
		if (sources > 1) {
			throw std::runtime_error("Context variable has multiple sources: " + name);
		}
		auto existing = std::find_if(descriptor.Keys.begin(), descriptor.Keys.end(), [&](const auto& value) { return value.Name == name; });
		const bool declared = existing != descriptor.Keys.end();
		DATA::VariableKeyDescriptor key = declared ? *existing : DATA::VariableKeyDescriptor {};
		if (declared && key.Type != variable.valueType()) {
			throw std::runtime_error("Context override type mismatch: " + name);
		}
		key.Name = name;
		key.Type = variable.valueType();
		if (sources > 0) {
			key.Expression = variable.Expression;
		}
		key.Access = variable.Property || variable.ObjectId ? DATA::VariableAccess::Input
			: key.Expression || variable.Script				? DATA::VariableAccess::Computed
			: declared										? key.Access
															: DATA::VariableAccess::Local;
		key.Event = variable.Type == VariableType::Event && key.Access == DATA::VariableAccess::Local;
		if (variable.Unset) {
			key.Default.reset();
		}
		else if (variable.Default) {
			key.Default = variable.Default;
		}
		else if (!declared && key.Type != DATA::ValueType::EntityRef) {
			DATA::LiteralDescriptor initial;
			initial.Type = key.Type;
			key.Default = initial;
		}
		if (declared) {
			*existing = std::move(key);
		}
		else {
			descriptor.Keys.push_back(std::move(key));
		}
	}
	auto schema = DATA::VariableSchema::compile(descriptor);
	auto candidate = std::make_shared<DATA::VariableContext>(schema);
	std::vector<Input> inputs;
	for (const auto& [name, variable] : settings.Variables) {
		const auto key = schema->key(name);
		if (variable.Property) {
			if (schema->keys[key].Type == DATA::ValueType::EntityRef) {
				throw std::runtime_error("EntityRef requires an entity source: " + name);
			}
			Input input;
			input.slot = key;
			input.property = PropertyRegistry::bind(scene, *variable.Property, schema->keys[key].Type);
			inputs.push_back(std::move(input));
		}
		else if (variable.ObjectId) {
			if (schema->keys[key].Type != DATA::ValueType::EntityRef || *variable.ObjectId <= 0) {
				throw std::runtime_error("Invalid context entity source: " + name);
			}
			auto object = scene.findObjectByID(Entity(Entity::ID(*variable.ObjectId)));
			if (!object || !object->isAlive()) {
				throw std::runtime_error("Missing context entity: " + name);
			}
			Input input;
			input.slot = key;
			input.entity = DATA::EntityRef { uint64_t(*variable.ObjectId), object,
				+[](const std::shared_ptr<void>& handle) { return std::static_pointer_cast<Object>(handle)->isAlive(); } };
			inputs.push_back(std::move(input));
		}
		else if (variable.Script) {
			if (schema->keys[key].Type != DATA::ValueType::Bool) {
				throw std::runtime_error("Script predicate output must be Bool");
			}
			const auto& script = *variable.Script;
			auto predicate = std::make_shared<SCRIPTING::ContextPredicate>(script.Path, script.Function);
			std::vector<size_t> arguments;
			for (const auto& name : script.Arguments) {
				const auto argument = schema->key(name);
				const auto type = schema->keys[argument].Type;
				if (type == DATA::ValueType::Vector3 || type == DATA::ValueType::EntityRef) {
					throw std::runtime_error("Predicate argument must be scalar");
				}
				arguments.push_back(argument);
			}
			candidate->bindComputation(
				key, arguments,
				[predicate, arguments](const DATA::VariableContext& context) -> std::optional<DATA::Value> {
					std::vector<DATA::Value> values;
					for (auto argument : arguments) {
						const auto& value = context.readCurrent(argument);
						if (!value) {
							return std::nullopt;
						}
						values.push_back(*value);
					}
					return predicate->evaluate(values);
				},
				true);
		}
	}
	for (size_t key = 0; key < schema->keys.size(); ++key) {
		if (schema->keys[key].Access == DATA::VariableAccess::Input
			&& std::none_of(inputs.begin(), inputs.end(), [&](const auto& input) { return input.slot == key; })) {
			throw std::runtime_error("Unbound context Input: " + schema->keys[key].Name);
		}
		if (schema->keys[key].Access == DATA::VariableAccess::Computed && !schema->expressions[key]) {
			const auto found = settings.Variables.find(schema->keys[key].Name);
			if (found == settings.Variables.end() || !found->second.Script) {
				throw std::runtime_error("Unbound context computation: " + schema->keys[key].Name);
			}
		}
	}
	if (mRuntime) {
		candidate->migrate(*mRuntime);
	}
	mRuntime = std::move(candidate);
	mInputs = std::move(inputs);
}
void VariableContextComponent::update(SCENE_SYSTEM::Scene& scene) {
	if (!mRuntime) {
		prepare(scene);
	}
	std::vector<std::optional<DATA::Value>> values;
	for (const auto& input : mInputs) {
		if (input.property) {
			values.push_back(input.property->read(scene));
		}
		else {
			values.push_back(input.entity->alive() ? std::optional<DATA::Value>(*input.entity) : std::nullopt);
		}
	}
	for (size_t index = 0; index < values.size(); ++index) {
		mRuntime->setInput(mInputs[index].slot, std::move(values[index]));
	}
	mRuntime->publish();
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VariableContextComponent>();
	return true;
}();
}
