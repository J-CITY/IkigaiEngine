#pragma once
#include "aiModule/behaviorTree/behaviorTree.h"
#include <queue>
#include <unordered_map>

namespace IKIGAI::AI {
class StraightLinePathQuery final : public PathQuery {
public:
	std::optional<std::vector<MATH::Vector3f>> findPath(MATH::Vector3f from, MATH::Vector3f to) const override;
};
class GridPathQuery final : public PathQuery {
public:
	explicit GridPathQuery(float cellSize = 1.f, int searchRadius = 128);
	std::optional<std::vector<MATH::Vector3f>> findPath(MATH::Vector3f from, MATH::Vector3f to) const override;

private:
	float mCellSize = 1.f;
	int mSearchRadius = 128;
};
void registerDefaultPathQuery();
}
