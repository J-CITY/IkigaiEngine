#include "cameraTransformEdit.h"

#if USE_JSON_VIEWS && !defined(OCULUS) && defined(USE_EDITOR)
#include "editorRender.h"
#include "prefabEditor.h"

#include <utility>

namespace IKIGAI::EDITOR {
namespace {
	class CameraTransformCommand final : public ICommand {
		std::weak_ptr<ECS::Object> mObject;
		std::weak_ptr<void> mSceneLifetime;
		MATH::Vector3f mBeforePosition;
		MATH::Vector3f mAfterPosition;
		MATH::QuaternionF mBeforeRotation;
		MATH::QuaternionF mAfterRotation;

		void apply(bool undoing) {
			auto target = mObject.lock();
			if (mSceneLifetime.expired() || !target || !target->isAlive() || IsCameraTransformLocked(*target)) {
				return;
			}
			auto transform = target->tryGetTransform();
			transform->setLocalPosition(undoing ? mBeforePosition : mAfterPosition);
			transform->setLocalRotation(undoing ? mBeforeRotation : mAfterRotation);
		}

	public:
		CameraTransformCommand(
			const std::shared_ptr<ECS::Object>& target, std::weak_ptr<void> sceneLifetime, MATH::Vector3f position, MATH::QuaternionF rotation)
			: mObject(target)
			, mSceneLifetime(std::move(sceneLifetime))
			, mBeforePosition(target->tryGetTransform()->getLocalPosition())
			, mAfterPosition(position)
			, mBeforeRotation(target->tryGetTransform()->getLocalRotation())
			, mAfterRotation(rotation) { }
		void execute() override {
			apply(false);
		}
		void undo() override {
			apply(true);
		}
		std::string getName() const override {
			return "Camera tools transform";
		}
	};
}

bool IsCameraTransformLocked(const ECS::Object& object) {
	return PREFABS::Locked(object);
}

void RecordCameraTransform(
	const std::shared_ptr<ECS::Object>& object, std::weak_ptr<void> sceneLifetime, MATH::Vector3f position, MATH::QuaternionF rotation) {
	EditorRender::GlobalState.mCommandHistory.addCommand(
		std::make_unique<CameraTransformCommand>(object, std::move(sceneLifetime), position, rotation));
}
}
#endif
