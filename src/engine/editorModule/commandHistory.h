#pragma once

#include <vector>
#include <memory>
#include <string>

#include "coreModule/ecs/object.h"
#include "reflectionModule/reflection.h"

namespace IKIGAI::EDITOR {

	class ICommand {
	public:
		virtual ~ICommand() = default;
		virtual void execute() = 0;
		virtual void undo() = 0;
		virtual std::string getName() const = 0;
	};

	class CommandHistory {
		std::vector<std::unique_ptr<ICommand>> mCommands;
		int mCurrentIndex = -1;

	public:
		void addCommand(std::unique_ptr<ICommand> cmd);
		void undo();
		void redo();
        void clear() { mCommands.clear(); mCurrentIndex = -1; }
		const std::vector<std::unique_ptr<ICommand>>& getCommands() const { return mCommands; }
		int getCurrentIndex() const { return mCurrentIndex; }
	};

	class ReflectionChangePropertyCommand : public ICommand {
		std::shared_ptr<ECS::Object> mObject;
		std::string mComponentTypeName;
		std::string mFieldName;
		UTILS::Any mOldValue;
		UTILS::Any mNewValue;
        std::weak_ptr<void> mSceneLifetime;
        int mObjectId = 0;

	public:
		ReflectionChangePropertyCommand(std::shared_ptr<ECS::Object> obj, const std::string& compType, const std::string& fieldName, UTILS::Any oldVal, UTILS::Any newVal);
		void execute() override;
		void undo() override;
		std::string getName() const override;

	private:
		void applyValue(const UTILS::Any& val);
	};

}
