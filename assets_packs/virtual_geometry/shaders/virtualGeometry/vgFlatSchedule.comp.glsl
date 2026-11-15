#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
#include "vgCullCommon.glsl"
#define VG_FLAT_SCHEDULE_BUILD
#include "vgFlatSelection.glsl"

layout(std430, set = 0, binding = 1) readonly buffer VgAssetDispatch {
	uvec4 Items[];
} engine_VgAssetDispatch;
layout(std430, set = 0, binding = 2) readonly buffer VgOccludedClusterCounts {
	uint Items[];
} engine_VgOccludedClusterCounts;
layout(std430, set = 0, binding = 3) writeonly buffer VgFlatDispatch {
	uvec4 Items[];
} engine_VgFlatDispatch;

uvec4 vgDispatchGrid(uint groups) {
	uint width = VgCullView.Asset.w;
	return uvec4(min(groups, width), (groups + width - 1u) / width, 1u, 0u);
}

void main() {
	uvec2 total = uvec2(0u);
	bool post = VgCullView.Pass.x == VG_PASS_POST_INSTANCES;
	// Only the small per-asset table is scanned here. Cluster work is dispatched in parallel.
	for (uint asset = 0u; asset < VgCullView.Pass.y; ++asset) {
		uvec4 instanceDispatch = engine_VgAssetDispatch.Items[asset];
		total.x += instanceDispatch.x * instanceDispatch.y;
		if (post) {
			uint count = min(engine_VgOccludedClusterCounts.Items[asset], engine_VgFlatAssets.Items[asset].Limits.x);
			total.y += (count + 63u) / 64u;
		}
		engine_VgFlatPrefix.Ends[asset] = total;
	}
	engine_VgFlatDispatch.Items[0] = vgDispatchGrid(total.x);
	engine_VgFlatDispatch.Items[1] = vgDispatchGrid(total.y);
}
