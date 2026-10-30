#pragma once

#include "component.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace IKIGAI::RENDER {
class MeshInterface;
class ModelInterface;
}

namespace IKIGAI::ECS {
class FacialAnimationComponent : public ComponentBase {
public:
	struct Weight {
		std::string Target {};
		float Value = 0;
		template <class C> static void serde(C& context, Weight& value) {
			serde::serde_struct(context, value).field(&Weight::Target, "Target").field(&Weight::Value, "Value");
		}
	};
	struct Key {
		float Time = 0;
		float Value = 0;
		template <class C> static void serde(C& context, Key& value) {
			serde::serde_struct(context, value).field(&Key::Time, "Time").field(&Key::Value, "Value");
		}
	};
	struct Track {
		std::string Target {};
		std::vector<Key> Keys {};
		template <class C> static void serde(C& context, Track& value) {
			serde::serde_struct(context, value).field(&Track::Target, "Target").field(&Track::Keys, "Keys");
		}
	};
	struct Clip {
		std::string Name {};
		float Duration = 0;
		bool Loop = true;
		std::vector<Track> Tracks {};
		template <class C> static void serde(C& context, Clip& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Clip::Name, "Name")
				.field(&Clip::Duration, "Duration")
				.field(&Clip::Loop, "Loop", default_ { true })
				.field(&Clip::Tracks, "Tracks");
		}
	};
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::vector<Weight> Weights {};
		std::vector<Clip> Clips {};
		std::string ActiveClip {};
		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "FacialAnimationComponentType")
				.field(&Descriptor::Weights, "Weights", default_ { std::vector<Weight> {} })
				.field(&Descriptor::Clips, "Clips", default_ { std::vector<Clip> {} })
				.field(&Descriptor::ActiveClip, "ActiveClip", default_ { std::string {} });
		}
	};

	explicit FacialAnimationComponent(UTILS::Ref<Object> object);
	FacialAnimationComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void bind(const RENDER::ModelInterface& model);
	void setWeight(const std::string& name, float value);
	float weight(const std::string& name) const;
	void clearWeights();
	void play(const std::string& name);
	void stop();
	std::vector<std::string> availableTargets() const;
	std::vector<std::string> availableClips() const;
	const std::array<float, 64>* weightsFor(const RENDER::MeshInterface& mesh) const;
	const std::array<float, 64>* previousWeightsFor(const RENDER::MeshInterface& mesh) const;
	void onUpdate(std::chrono::duration<double> elapsed) override;
	void onDisable() override;

private:
	using TargetSlot = std::pair<std::size_t, std::size_t>;
	struct ResolvedTrack {
		std::vector<TargetSlot> Slots {};
		std::vector<Key> Keys {};
	};
	struct ResolvedClip {
		float Duration = 0;
		bool Loop = true;
		std::vector<ResolvedTrack> Tracks {};
	};
	Descriptor mDescriptor {};
	const RENDER::ModelInterface* mBoundModel = nullptr;
	std::vector<const RENDER::MeshInterface*> mMeshes {};
	std::unordered_map<std::string, std::vector<TargetSlot>> mTargets {};
	std::vector<std::array<float, 64>> mCurrent {};
	std::vector<std::array<float, 64>> mPrevious {};
	std::unordered_map<std::string, std::size_t> mClipNames {};
	std::vector<ResolvedClip> mClips {};
	std::size_t mActiveClip = SIZE_MAX;
	float mTime = 0;
};

template <> inline std::string GetType<FacialAnimationComponent>() {
	return "class IKIGAI::ECS::FacialAnimationComponent";
}
template <> inline std::string GetComponentName<FacialAnimationComponent>() {
	return "FacialAnimationComponent";
}
}
