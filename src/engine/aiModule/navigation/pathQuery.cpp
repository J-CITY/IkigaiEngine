#include "pathQuery.h"
#include "resourceModule/serviceManager.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::AI {
namespace {
	struct GridCell {
		int x = 0;
		int z = 0;
		bool operator==(const GridCell&) const = default;
	};
	struct GridCellHash {
		size_t operator()(const GridCell& cell) const {
			return std::hash<int>()(cell.x) ^ (std::hash<int>()(cell.z) << 1);
		}
	};
	MATH::Vector3f snapToCell(MATH::Vector3f point, float cellSize) {
		return { std::round(point.x / cellSize) * cellSize, point.y, std::round(point.z / cellSize) * cellSize };
	}
}
std::optional<std::vector<MATH::Vector3f>> StraightLinePathQuery::findPath(MATH::Vector3f from, MATH::Vector3f to) const {
	if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(from.z) || !std::isfinite(to.x) || !std::isfinite(to.y)
		|| !std::isfinite(to.z)) {
		return std::nullopt;
	}
	return std::vector<MATH::Vector3f> { from, to };
}
GridPathQuery::GridPathQuery(float cellSize, int searchRadius)
	: mCellSize(cellSize)
	, mSearchRadius(searchRadius) {
	if (!std::isfinite(cellSize) || cellSize <= 0 || searchRadius <= 0) {
		throw std::runtime_error("GridPathQuery requires positive cellSize and searchRadius");
	}
}
std::optional<std::vector<MATH::Vector3f>> GridPathQuery::findPath(MATH::Vector3f from, MATH::Vector3f to) const {
	if (!std::isfinite(from.x) || !std::isfinite(from.z) || !std::isfinite(to.x) || !std::isfinite(to.z)) {
		return std::nullopt;
	}
	const auto start = snapToCell(from, mCellSize);
	const auto goal = snapToCell(to, mCellSize);
	const GridCell origin { int(std::round(start.x / mCellSize)), int(std::round(start.z / mCellSize)) };
	const GridCell target { int(std::round(goal.x / mCellSize)), int(std::round(goal.z / mCellSize)) };
	if (origin == target) {
		return std::vector<MATH::Vector3f> { from, to };
	}
	std::queue<GridCell> open {};
	std::unordered_map<GridCell, GridCell, GridCellHash> parents {};
	std::unordered_map<GridCell, bool, GridCellHash> visited {};
	open.push(origin);
	visited[origin] = true;
	const auto inBounds
		= [&](const GridCell& cell) { return std::abs(cell.x - origin.x) <= mSearchRadius && std::abs(cell.z - origin.z) <= mSearchRadius; };
	while (!open.empty()) {
		const auto current = open.front();
		open.pop();
		if (current == target) {
			break;
		}
		for (const GridCell delta : { GridCell { 1, 0 }, GridCell { -1, 0 }, GridCell { 0, 1 }, GridCell { 0, -1 } }) {
			const GridCell next { current.x + delta.x, current.z + delta.z };
			if (!inBounds(next) || visited.contains(next)) {
				continue;
			}
			visited[next] = true;
			parents[next] = current;
			open.push(next);
		}
	}
	if (!visited.contains(target)) {
		return StraightLinePathQuery {}.findPath(from, to);
	}
	std::vector<GridCell> cells {};
	for (auto cell = target; cell != origin; cell = parents.at(cell)) {
		cells.push_back(cell);
	}
	cells.push_back(origin);
	std::reverse(cells.begin(), cells.end());
	std::vector<MATH::Vector3f> path {};
	path.reserve(cells.size() + 1);
	path.push_back(from);
	for (size_t index = 1; index + 1 < cells.size(); ++index) {
		path.push_back({ cells[index].x * mCellSize, from.y, cells[index].z * mCellSize });
	}
	path.push_back(to);
	return path;
}
void registerDefaultPathQuery() {
	static StraightLinePathQuery query {};
	if (!RESOURCES::ServiceManager::Check<PathQuery>()) {
		RESOURCES::ServiceManager::Set<PathQuery>(&query);
	}
}
}
