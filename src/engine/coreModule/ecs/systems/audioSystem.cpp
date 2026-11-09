#include "audioSystem.h"

#include "audioModule/audioManager.h"
#include "coreModule/ecs/components/audioComponent.h"
#include "coreModule/ecs/components/transform.h"
#include "ecsModule/systemManager.h"
#include "ecsModule/world.h"

#include <memory>
#include <optional>
#include <vector>

namespace IKIGAI::ECS {
namespace {
	constexpr int audioPriority = 30;

	struct ListenerPose {
		MATH::Vector3f position {};
		MATH::Vector3f direction {};
	};

	struct SourcePose {
		ComponentHandle<AudioComponent> source {};
		MATH::Vector3f position {};
	};

	struct ListenerFrame {
		std::optional<ListenerPose> pose {};
	};

	struct SourceFrame {
		std::vector<SourcePose> poses {};
	};

	struct AudioSpatialFrame {
		ListenerFrame listener {};
		SourceFrame sources {};
	};

	class AudioListenerGatherSystem final : public System {
	public:
		AudioListenerGatherSystem(const ComponentManager& components, std::shared_ptr<AudioSpatialFrame> frame)
			: mFrame(std::move(frame)) {
			mName = "AudioListenerGatherSystem";
			mPriority = audioPriority;
			mValidateAccess = true;
			mReads.insert(components.getComponentType<AudioListenerComponent>());
			mReads.insert(components.getComponentType<TransformComponent>());
			writesResource<ListenerFrame>();
		}

		void onLateUpdate(World& world, CommandBuffer&, std::chrono::duration<double>) override {
			auto& listener = writeResource(mFrame->listener).pose;
			listener.reset();
			auto& components = *world.getComponentManager();
			// The first listener is chosen in sparse pool order, even after a swap-removal.
			components.forEachSparse<Read<AudioListenerComponent>>([&](Entity entity, const AudioListenerComponent&) {
				if (!listener) {
					if (const auto* transform = std::as_const(components).tryGetComponent<TransformComponent>(entity)) {
						listener = ListenerPose { transform->getWorldPosition(), transform->getWorldRotation() * -MATH::Vector3f::Forward };
					}
				}
			});
		}

	private:
		std::shared_ptr<AudioSpatialFrame> mFrame;
	};

	class AudioSourceGatherSystem final : public System {
	public:
		AudioSourceGatherSystem(const ComponentManager& components, std::shared_ptr<AudioSpatialFrame> frame)
			: mFrame(std::move(frame)) {
			mName = "AudioSourceGatherSystem";
			mPriority = audioPriority;
			mValidateAccess = true;
			mReads.insert(components.getComponentType<AudioComponent>());
			mReads.insert(components.getComponentType<TransformComponent>());
			writesResource<SourceFrame>();
		}

		void onLateUpdate(World& world, CommandBuffer&, std::chrono::duration<double>) override {
			auto& poses = writeResource(mFrame->sources).poses;
			poses.clear();
			auto& components = *world.getComponentManager();
			components.forEachSparse<Read<AudioComponent>>([&](Entity entity, const AudioComponent&) {
				if (const auto* transform = std::as_const(components).tryGetComponent<TransformComponent>(entity)) {
					poses.push_back(SourcePose { components.getComponentHandle<AudioComponent>(entity), transform->getWorldPosition() });
				}
			});
		}

	private:
		std::shared_ptr<AudioSpatialFrame> mFrame;
	};

	class AudioSystem final : public System {
	public:
		AudioSystem(const ComponentManager& components, std::shared_ptr<AudioSpatialFrame> frame)
			: mFrame(std::move(frame)) {
			mName = "AudioSystem";
			mPriority = audioPriority;
			mExclusiveBatch = true;
			mValidateAccess = true;
			mReads.insert(components.getComponentType<AudioComponent>());
			readsResource<ListenerFrame>();
			readsResource<SourceFrame>();
			writesResource<AUDIO::AudioManagerAL>();
			runAfter("AudioListenerGatherSystem");
			runAfter("AudioSourceGatherSystem");
		}

		void onLateUpdate(World& world, CommandBuffer&, std::chrono::duration<double>) override {
			const auto& listener = readResource(mFrame->listener).pose;
			const auto& sources = readResource(mFrame->sources).poses;
			const auto& components = *world.getComponentManager();
			if (listener) {
				writeResource(AUDIO::AudioManagerAL::GetInstance()).setListenerPos(listener->position, listener->direction);
			}
			for (const auto& pose : sources) {
				const auto* audio = components.resolveComponent(pose.source);
				if (audio && audio->res && audio->res->data.is3D) {
					writeResource(AUDIO::AudioManagerAL::GetInstance()).setSourcePos(*audio->res, pose.position);
				}
			}
		}

	private:
		std::shared_ptr<const AudioSpatialFrame> mFrame;
	};
}

void registerAudioSystems(SystemManager& systems) {
	auto frame = std::make_shared<AudioSpatialFrame>();
	const auto& components = *systems.getComponentManager();
	systems.registerSystem<AudioListenerGatherSystem>(components, frame);
	systems.registerSystem<AudioSourceGatherSystem>(components, frame);
	systems.registerSystem<AudioSystem>(components, std::move(frame));
}
}
