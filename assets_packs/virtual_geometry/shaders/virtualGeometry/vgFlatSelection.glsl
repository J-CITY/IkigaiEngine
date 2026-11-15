struct VgFlatAsset {
	uvec4 Range;   // cluster base, cluster count, visible instance base, occluded cluster base
	uvec4 Limits;  // occluded cluster capacity, padding
};

layout(std430, set = 0, binding = 17) readonly buffer VgFlatAssets {
	VgFlatAsset Items[];
} engine_VgFlatAssets;

#ifdef VG_FLAT_SCHEDULE_BUILD
layout(std430, set = 0, binding = 18) writeonly buffer VgFlatPrefix {
	uvec2 Ends[];
} engine_VgFlatPrefix;
#else
layout(std430, set = 0, binding = 18) readonly buffer VgFlatPrefix {
	uvec2 Ends[];
} engine_VgFlatPrefix;

// Zero-work assets can repeat an end. Search for the first end strictly above this group.
bool vgFlatWorkgroup(uint lane, out uint asset, out uint groupInAsset) {
	uint count = VgCullView.Pass.y;
	uint group = gl_WorkGroupID.x + gl_WorkGroupID.y * VgCullView.Asset.w;
	if (group >= engine_VgFlatPrefix.Ends[count - 1u][lane]) {
		return false;
	}
	uint left = 0u;
	uint right = count - 1u;
	while (left < right) {
		uint middle = left + (right - left) / 2u;
		if (engine_VgFlatPrefix.Ends[middle][lane] > group) {
			right = middle;
		}
		else {
			left = middle + 1u;
		}
	}
	asset = left;
	groupInAsset = group - (asset == 0u ? 0u : engine_VgFlatPrefix.Ends[asset - 1u][lane]);
	return true;
}
#endif
