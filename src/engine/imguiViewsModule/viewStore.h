#pragma once
#include "view.h"
#include <set>

namespace IKIGAI::UI {
struct ViewInstance {
	ViewDefinition definition;
	std::shared_ptr<Controller> controller;
	bool open = true;
	bool faulted = false;
	struct Edit {
		Json before;
		bool editing = false;
		std::string text;
	};
	std::map<std::string, Edit> edits;
	void finishEdit(const Node& node);
	void finishEdits(const std::vector<Node>& nodes);
};
// IO is supplied by the host; the exact reload transaction is also testable
// without a scene, graphics device, or native filesystem.
class ViewStore {
public:
	using Reader = std::function<std::string(const std::string&)>;
	using Factory = std::function<std::shared_ptr<Controller>()>;
	struct Entry {
		std::unique_ptr<ViewInstance> instance;
		std::string attemptedSource, attemptedScript, attemptedScriptPath, error;
	};
	std::map<std::string, Entry> windows;
	std::map<std::string, Factory> factories;
	std::set<std::string> customWidgets;
	// False means failed or unchanged. Last good instance survives a failure.
	bool reload(const std::string& path, const Reader& read, bool force = false);
};
}
