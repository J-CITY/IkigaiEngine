#include "guiAnimationComponent.h"

#include "coreModule/ecs/animationTimelineBind.h"
#include "coreModule/ecs/componentCatalog.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/log/logger.h"
#include "utilsModule/serialization/typedDescriptor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>

namespace IKIGAI::ECS {
namespace {
	using namespace MATH::CURVES;

	struct Segment {
		float start = 0.0f;
		float end = 0.0f;
		float value = 0.0f;
		InterpolationType easing = InterpolationType::LINEAR;
		MATH::Vector2f control1 {};
		MATH::Vector2f control2 {};
	};

	using Destination = std::pair<std::string, GuiAnimationProperty>;
	using Segments = std::map<Destination, std::vector<Segment>>;
	using Curves = std::map<Destination, FloatCurve>;

	float visitNode(const GuiAnimationNode& node, float start, Segments& segments) {
		if (node.Kind == GuiAnimationNodeKind::Sequence || node.Kind == GuiAnimationNodeKind::Parallel) {
			if (node.Children.empty()) {
				throw std::invalid_argument("A Sequence or Parallel node needs children");
			}
			float end = start;
			for (const auto& child : node.Children) {
				const float childStart = node.Kind == GuiAnimationNodeKind::Sequence ? end : start;
				end = std::max(end, visitNode(child, childStart, segments));
			}
			return end;
		}
		const float end = start + node.Time;
		if (!node.Children.empty() || !std::isfinite(end) || end <= start) {
			throw std::invalid_argument("Tween and Wait nodes need a positive finite duration and no children");
		}
		if (node.Kind == GuiAnimationNodeKind::Tween) {
			segments[{ node.Target, node.Property }].push_back({ start, end, node.Value, node.Easing, node.Control1, node.Control2 });
		}
		else if (node.Kind != GuiAnimationNodeKind::Wait) {
			throw std::invalid_argument("Unknown GUI animation node kind");
		}
		return end;
	}

	FloatCurve compileCurve(GuiAnimationProperty property, std::vector<Segment>& segments) {
		if (segments.empty()) {
			throw std::invalid_argument("An animation needs at least one key");
		}
		FloatCurve curve;
		// The first value is captured separately for each output when its event fires.
		curve.keys.push_back({ segments.front().start, 0.0f });
		for (const auto& segment : segments) {
			if (segment.start < curve.keys.back().time || segment.end <= segment.start) {
				throw std::invalid_argument("Keys must advance in time and parallel tweens on one property must not overlap");
			}
			if (property == GuiAnimationProperty::Opacity && (segment.value < 0 || segment.value > 1)) {
				throw std::invalid_argument("Opacity values must be in [0,1]");
			}
			if (segment.start > curve.keys.back().time) {
				// A gap between two tweens holds the previous endpoint.
				curve.keys.push_back({ segment.start, curve.keys.back().value });
			}
			auto& outgoing = curve.keys.back();
			outgoing.interpolation = segment.easing == InterpolationType::CUSTOM ? SegmentInterpolation::Bezier : SegmentInterpolation::Easing;
			outgoing.easing = segment.easing;
			outgoing.outControl = segment.control1;
			outgoing.inControl = segment.control2;
			curve.keys.push_back({ segment.end, segment.value });
		}
		curve.validate();
		return curve;
	}

	Curves compileGraph(const GuiAnimationNode& root) {
		Segments segments;
		visitNode(root, 0.0f, segments);
		if (segments.empty()) {
			throw std::invalid_argument("A GUI animation graph needs a Tween node");
		}
		Curves curves;
		for (auto& [destination, spans] : segments) {
			std::sort(spans.begin(), spans.end(), [](const Segment& lhs, const Segment& rhs) { return lhs.start < rhs.start; });
			curves.emplace(destination, compileCurve(destination.second, spans));
		}
		return curves;
	}

	Curves loadGraph(const std::string& path) {
		auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
		try {
			const auto file = files.getLiveFile(path);
			if (!file) {
				throw std::invalid_argument("File could not be opened");
			}
			return compileGraph(DATA::readDescriptor<GuiAnimationNode>(nlohmann::json::parse(file->readStr())));
		} catch (const std::exception& error) {
			throw std::invalid_argument(path + ": " + error.what());
		}
	}

	Object& resolveTarget(Object& owner, const std::string& name) {
		if (name.empty()) {
			return owner;
		}
		Object* target = nullptr;
		for (const auto& child : owner.getChildren()) {
			if (child && child->getName() == name) {
				if (target) {
					throw std::invalid_argument("Ambiguous child '" + name + "' on '" + owner.getName() + "'");
				}
				target = child.get();
			}
		}
		if (!target) {
			throw std::invalid_argument("Missing child '" + name + "' on '" + owner.getName() + "'");
		}
		return *target;
	}

	struct Output {
		ANIMATION::AnimationProperty binding {};
		ANIMATION::PropType initialValue = 0.0f;
	};

	template <class Component, class Value = float, class Setter, class Getter>
	void bindOutput(std::vector<Output>& outputs, Object& target, Setter setter, Getter getter) {
		auto& world = target.getWorld();
		const auto& manager = *world.getComponentManager();
		const auto handle = manager.getComponentHandle<Component>(target.getID());
		if (manager.resolveComponent(handle)) {
			outputs.push_back({ ANIMATION::detail::makeComponentProperty<Value>(
				"", world.getComponentManagerWeak(), handle, setter, getter, InterpolationType::LINEAR) });
		}
	}

	template <class Component> void bindAlpha(std::vector<Output>& outputs, Object& target, MATH::Vector4f Component::* color) {
		bindOutput<Component>(
			outputs, target, [color](Component& component, float value) { (component.*color).w = std::clamp(value, 0.0f, 1.0f); },
			[color](const Component& component) { return (component.*color).w; });
	}

	std::vector<Output> bindProperty(Object& target, GuiAnimationProperty property) {
		std::vector<Output> outputs;
		switch (property) {
		case GuiAnimationProperty::Scale:
			bindOutput<TransformComponent, MATH::Vector2f>(
				outputs, target,
				[](TransformComponent& component, MATH::Vector2f value) {
					component.setLocalScale({ value.x, value.y, component.getLocalScale().z });
				},
				[](const TransformComponent& component) {
					const auto& scale = component.getLocalScale();
					return MATH::Vector2f(scale.x, scale.y);
				});
			break;
		case GuiAnimationProperty::Opacity:
			bindAlpha(outputs, target, &VectorShapeComponent::Color);
			bindAlpha(outputs, target, &SdfTextComponent::Color);
			bindAlpha(outputs, target, &SpriteComponent::mColor);
			bindAlpha<SpriteAnimateComponent>(outputs, target, &SpriteComponent::mColor);
			bindAlpha(outputs, target, &LabelComponent::color);
			break;
		case GuiAnimationProperty::PositionX:
		case GuiAnimationProperty::PositionY: {
			const auto axis = property == GuiAnimationProperty::PositionX ? &MATH::Vector3f::x : &MATH::Vector3f::y;
			bindOutput<TransformComponent>(
				outputs, target,
				[axis](TransformComponent& component, float value) {
					auto position = component.getLocalPosition();
					position.*axis = value;
					component.setLocalPosition(position);
				},
				[axis](const TransformComponent& component) { return component.getLocalPosition().*axis; });
			break;
		}
		case GuiAnimationProperty::RotationZ:
			bindOutput<TransformComponent>(
				outputs, target,
				[](TransformComponent& component, float degrees) {
					auto rotation = component.getLocalRotationDeg();
					rotation.z = degrees;
					component.setLocalRotationDeg(rotation);
				},
				[](const TransformComponent& component) { return component.getLocalRotationDeg().z; });
			break;
		}
		if (outputs.empty()) {
			throw std::invalid_argument("The target '" + target.getName() + "' has no component for this animation property");
		}
		return outputs;
	}
}

void validateGuiAnimationGraph(const GuiAnimationNode& root) {
	compileGraph(root);
}

class GuiAnimationProgram {
	struct Track {
		GUI::EventType event = GUI::EventType::HoverIn;
		std::size_t channel = 0;
		MATH::CURVES::FloatCurve curve {};
	};
	struct Channel {
		Entity target {};
		GuiAnimationProperty property = GuiAnimationProperty::Scale;
		std::vector<Output> outputs {};
		std::optional<std::size_t> playing {};
		float elapsed = 0.0f;
	};
	std::vector<Track> mTracks {};
	std::vector<Channel> mChannels {};

	void addTrack(Object& target, GuiAnimationProperty property, GUI::EventType event, MATH::CURVES::FloatCurve curve) {
		const auto found = std::find_if(mChannels.begin(), mChannels.end(),
			[&](const Channel& channel) { return channel.target == target.getID() && channel.property == property; });
		const auto channel = static_cast<std::size_t>(found - mChannels.begin());
		if (found == mChannels.end()) {
			mChannels.push_back({ target.getID(), property, bindProperty(target, property) });
		}
		if (std::any_of(mTracks.begin(), mTracks.end(), [&](const Track& track) { return track.event == event && track.channel == channel; })) {
			throw std::invalid_argument("Two bindings for this event animate the same property on '" + target.getName() + "'");
		}
		mTracks.push_back({ event, channel, std::move(curve) });
	}

public:
	GuiAnimationProgram(Object& owner, const std::vector<GuiAnimationBinding>& bindings) {
		std::map<std::string, Curves> graphs;
		for (std::size_t i = 0; i < bindings.size(); ++i) {
			const auto& binding = bindings[i];
			try {
				auto& target = resolveTarget(owner, binding.Target);
				if (binding.GraphPath.empty()) {
					std::vector<Segment> segments;
					float start = 0.0f;
					for (const auto& key : binding.Keys) {
						segments.push_back({ start, key.Time, key.Value, key.Easing, key.Control1, key.Control2 });
						start = key.Time;
					}
					addTrack(target, binding.Property, binding.Event, compileCurve(binding.Property, segments));
				}
				else {
					if (!binding.Keys.empty()) {
						throw std::invalid_argument("Choose inline Keys or GraphPath");
					}
					auto [entry, inserted] = graphs.try_emplace(binding.GraphPath);
					if (inserted) {
						entry->second = loadGraph(binding.GraphPath);
					}
					for (const auto& [destination, curve] : entry->second) {
						addTrack(resolveTarget(target, destination.first), destination.second, binding.Event, curve);
					}
				}
			} catch (const std::exception& error) {
				throw std::invalid_argument("Binding " + std::to_string(i + 1) + ": " + error.what());
			}
		}
	}

	void trigger(GUI::EventType event) {
		for (std::size_t i = 0; i < mTracks.size(); ++i) {
			const auto& track = mTracks[i];
			if (track.event == event) {
				auto& channel = mChannels[track.channel];
				channel.playing = i;
				channel.elapsed = 0.0f;
				for (auto& output : channel.outputs) {
					output.initialValue = output.binding.get();
				}
			}
		}
	}

	void advance(float seconds) {
		if (!(seconds > 0.0f) || !std::isfinite(seconds)) {
			return;
		}
		for (auto& channel : mChannels) {
			if (!channel.playing) {
				continue;
			}
			auto& curve = mTracks[*channel.playing].curve;
			const float end = curve.keys.back().time;
			channel.elapsed = std::min(end, channel.elapsed + seconds);
			auto evaluate = [&](float initialValue) {
				curve.keys.front().value = initialValue;
				return curve.evaluate(channel.elapsed);
			};
			for (auto& output : channel.outputs) {
				if (const auto* scale = std::get_if<MATH::Vector2f>(&output.initialValue)) {
					output.binding.set(MATH::Vector2f { evaluate(scale->x), evaluate(scale->y) });
				}
				else {
					output.binding.set(evaluate(std::get<float>(output.initialValue)));
				}
			}
			if (channel.elapsed >= end) {
				channel.playing.reset();
			}
		}
	}
};

GuiAnimationComponent::GuiAnimationComponent(UTILS::Ref<Object> object)
	: GuiAnimationComponent(object, Descriptor {}) { }

GuiAnimationComponent::GuiAnimationComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ComponentBase(object)
	, mBindings(descriptor.Bindings) {
	__NAME__ = GetComponentName<GuiAnimationComponent>();
}

GuiAnimationComponent::GuiAnimationComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: GuiAnimationComponent(object, static_cast<const Descriptor&>(descriptor)) { }

GuiAnimationComponent::GuiAnimationComponent(GuiAnimationComponent&&) noexcept = default;
GuiAnimationComponent& GuiAnimationComponent::operator=(GuiAnimationComponent&&) noexcept = default;
GuiAnimationComponent::~GuiAnimationComponent() = default;

GuiAnimationComponent::Descriptor GuiAnimationComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<GuiAnimationComponent>();
	descriptor.Bindings = mBindings;
	return descriptor;
}

const std::vector<GuiAnimationBinding>& GuiAnimationComponent::bindings() const {
	return mBindings;
}

bool GuiAnimationComponent::setBindings(std::vector<GuiAnimationBinding> bindings) {
	try {
		auto program = std::make_unique<GuiAnimationProgram>(obj.get(), bindings);
		mProgram = std::move(program);
		mBindings = std::move(bindings);
		mValidationError.clear();
		return true;
	} catch (const std::exception& error) {
		mValidationError = error.what();
		return false;
	}
}

const std::string& GuiAnimationComponent::validationError() const {
	return mValidationError;
}

void GuiAnimationComponent::onEnable() {
	if (!setBindings(mBindings)) {
		LOG_WARNING << "GuiAnimationComponent '" << obj->getName() << "': " << mValidationError;
	}
}

void GuiAnimationComponent::onDisable() {
	mProgram.reset();
}

void GuiAnimationComponent::trigger(GUI::EventType event) {
	if (mProgram) {
		mProgram->trigger(event);
	}
}

void GuiAnimationComponent::advance(float seconds) {
	if (mProgram) {
		mProgram->advance(seconds);
	}
}

namespace {
	[[maybe_unused]] const bool registered = ComponentCatalog::enroll<GuiAnimationComponent>();
}
}
