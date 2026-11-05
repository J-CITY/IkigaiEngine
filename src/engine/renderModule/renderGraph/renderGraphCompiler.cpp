#include "renderGraphCompiler.h"
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER::GRAPH {
namespace {
	struct Version {
		std::optional<PassId> Writer;
		std::vector<PassId> Readers;
	};

	struct AccessPair {
		const Use* Read = nullptr;
		const Use* Write = nullptr;
	};

	bool IsStorage(Access access) {
		return access == Access::STORAGE_READ || access == Access::STORAGE_WRITE;
	}

	bool CanUpdateInPlace(Access read, Access write) {
		return (read == Access::STORAGE_READ && write == Access::STORAGE_WRITE)
			|| (read == Access::ATTACHMENT_READ && write == Access::ATTACHMENT_WRITE);
	}
}

bool IsWrite(Access access) {
	return access == Access::STORAGE_WRITE || access == Access::ATTACHMENT_WRITE || access == Access::TRANSFER_WRITE;
}

Plan Compile(std::span<const Resource> resources, std::span<const Pass> passes) {
	std::vector<std::map<uint32_t, Version>> versions(resources.size());
	std::vector<std::map<ResourceId, AccessPair>> accesses(passes.size());
	std::vector<std::set<PassId>> edges(passes.size());
	std::vector<std::vector<PassId>> producers(passes.size());
	std::vector<size_t> indegree(passes.size());
	std::vector<PassId> roots;
	const auto connect = [&](PassId from, PassId to) {
		if (edges[from].insert(to).second) {
			++indegree[to];
		}
	};

	for (PassId id = 0; id < passes.size(); ++id) {
		const auto& pass = passes[id];
		if (pass.SideEffect) {
			roots.push_back(id);
		}
		for (auto dependency : pass.After) {
			if (dependency >= passes.size() || dependency == id) {
				throw std::invalid_argument("Invalid dependency for graph pass " + pass.Name);
			}
			connect(dependency, id);
		}
		for (const auto& use : pass.Uses) {
			if (use.Resource >= resources.size()) {
				throw std::invalid_argument("Invalid resource for graph pass " + pass.Name);
			}
			auto& pair = accesses[id][use.Resource];
			auto& slot = IsWrite(use.Mode) ? pair.Write : pair.Read;
			if (slot) {
				throw std::invalid_argument("Duplicate resource access in graph pass " + pass.Name);
			}
			slot = &use;
			auto& version = versions[use.Resource][use.Version];
			if (IsWrite(use.Mode)) {
				if (!use.Version || version.Writer) {
					throw std::invalid_argument("Invalid or duplicate writer of " + resources[use.Resource].Name);
				}
				version.Writer = id;
			}
			else {
				version.Readers.push_back(id);
			}
		}
		for (const auto& [resource, pair] : accesses[id]) {
			if (pair.Read && pair.Write && (pair.Write->Version - 1 != pair.Read->Version || !CanUpdateInPlace(pair.Read->Mode, pair.Write->Mode))) {
				throw std::invalid_argument("Incompatible in-place access to " + resources[resource].Name + " in " + pass.Name);
			}
		}
	}

	for (ResourceId resource = 0; resource < resources.size(); ++resource) {
		const auto& desc = resources[resource];
		auto& timeline = versions[resource];
		if (desc.Imported) {
			timeline.try_emplace(0);
		}
		if (desc.Exported && timeline.empty()) {
			throw std::invalid_argument("Graph export has no source: " + desc.Name);
		}
		const Version* previous = nullptr;
		uint32_t expected = desc.Imported ? 0 : 1;
		for (const auto& [number, version] : timeline) {
			if (number != expected++ || (number != 0 && !version.Writer)) {
				throw std::invalid_argument("Graph resource version has no producer: " + desc.Name + "@" + std::to_string(number));
			}
			if (version.Writer) {
				const auto writer = *version.Writer;
				for (auto reader : version.Readers) {
					connect(writer, reader);
					producers[reader].push_back(writer);
				}
				// All versions share the same allocation. Finish using the old contents
				// before overwriting them, even if no pass reads the new contents.
				if (previous) {
					if (previous->Writer) {
						connect(*previous->Writer, writer);
					}
					for (auto reader : previous->Readers) {
						if (reader != writer) {
							connect(reader, writer);
						}
					}
				}
			}
			previous = &version;
		}
		if (desc.Exported && previous && previous->Writer) {
			roots.push_back(*previous->Writer);
		}
	}

	std::priority_queue<PassId, std::vector<PassId>, std::greater<>> ready;
	for (PassId id = 0; id < passes.size(); ++id) {
		if (indegree[id] == 0) {
			ready.push(id);
		}
	}
	std::vector<PassId> order;
	while (!ready.empty()) {
		const auto id = ready.top();
		ready.pop();
		order.push_back(id);
		for (auto next : edges[id]) {
			if (--indegree[next] == 0) {
				ready.push(next);
			}
		}
	}
	if (order.size() != passes.size()) {
		std::string names;
		for (PassId id = 0; id < passes.size(); ++id) {
			if (indegree[id]) {
				names += " " + passes[id].Name;
			}
		}
		throw std::invalid_argument("Render graph has a dependency cycle involving" + names);
	}

	std::vector<bool> live(passes.size());
	while (!roots.empty()) {
		const auto id = roots.back();
		roots.pop_back();
		if (!live[id]) {
			live[id] = true;
			roots.insert(roots.end(), producers[id].begin(), producers[id].end());
		}
	}

	Plan result;
	std::vector<const Use*> last(resources.size());
	for (auto id : order) {
		if (!live[id]) {
			result.Culled.push_back(id);
			continue;
		}
		ScheduledPass scheduled { id, {} };
		for (const auto& [resource, pair] : accesses[id]) {
			const auto* current = pair.Write ? pair.Write : pair.Read;
			const auto* previous = last[resource];
			if ((previous && (IsWrite(previous->Mode) || pair.Write)) || (!previous && resources[resource].Imported)) {
				scheduled.Barriers.push_back({ resource, IsStorage(current->Mode) || (previous && IsStorage(previous->Mode)) });
			}
			last[resource] = current;
		}
		result.Schedule.push_back(std::move(scheduled));
	}
	std::optional<PassId> outstandingCompute;
	const auto needsCompute = [&](PassId compute, PassId graphics) {
		if (edges[compute].contains(graphics)) {
			return true;
		}
		// Even concurrent reads may use different native buffer states.
		for (const auto& [resource, use] : accesses[compute]) {
			if (accesses[graphics].contains(resource)) {
				return true;
			}
		}
		return false;
	};
	for (size_t index = 0; index < result.Schedule.size(); ++index) {
		auto& pass = result.Schedule[index];
		if (outstandingCompute && needsCompute(*outstandingCompute, pass.Id)) {
			pass.WaitForCompute = true;
			outstandingCompute.reset();
		}
		// Skip a queue handoff unless at least the next pass can run concurrently.
		if (!outstandingCompute && passes[pass.Id].PreferredQueue == Queue::ASYNC_COMPUTE && index + 1 < result.Schedule.size()
			&& !needsCompute(pass.Id, result.Schedule[index + 1].Id)) {
			pass.ExecutionQueue = Queue::ASYNC_COMPUTE;
			outstandingCompute = pass.Id;
		}
	}
	return result;
}
}
