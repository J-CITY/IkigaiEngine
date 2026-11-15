// The HZB holds depth rendered with viewProjection (standard depth, 0 = near, MAX reduction, mip k is
// max(1, floor(size / 2^k)) and every mip-k texel j covers at least the mip-0 range [j * size / d_k, (j + 1) * size / d_k)).
// Conservative: true only when the whole world-space sphere is certainly behind the stored depth. Every doubtful case
// (camera plane or near plane crossed, NaN, off-screen rect, too many texels) answers "visible".
bool hzbSphereOccluded(sampler2D hzb, vec4 viewProjection[4], vec3 center, float radius, bool unsafe) {
	if (!(radius > 0.0)) {
		return false;
	}
	vec2 low = vec2(3.0e38);
	vec2 high = vec2(-3.0e38);
	float nearest = 3.0e38;
	// Projection of the sphere's world AABB: the convex hull of its projected corners bounds the projected sphere, and
	// view depth is linear over the box, so the nearest corner is the nearest point of the box.
	for (int i = 0; i < 8; ++i) {
		vec4 corner = vec4(center + vec3((i & 1) != 0 ? radius : -radius, (i & 2) != 0 ? radius : -radius, (i & 4) != 0 ? radius : -radius), 1.0);
		vec4 clip = vec4(dot(viewProjection[0], corner), dot(viewProjection[1], corner),
			dot(viewProjection[2], corner), dot(viewProjection[3], corner));
		if (!(clip.w > 1.0e-5)) {
			return false;
		}
		vec3 ndc = clip.xyz / clip.w;
		low = min(low, ndc.xy);
		high = max(high, ndc.xy);
		nearest = min(nearest, ndc.z);
	}
	if (!(nearest >= 0.0)) {
		return false;
	}
	// NDC -> mip-0 pixels. Vulkan and DirectX 12 share the convention of the G-buffer velocity (vgGBuffer.frag):
	// uv.y = 1 - (ndc.y * 0.5 + 0.5), row 0 at the top. One pixel of margin covers rasterization rules and rounding.
	vec2 size = vec2(textureSize(hzb, 0));
	vec2 first = clamp(vec2(low.x * 0.5 + 0.5, 0.5 - high.y * 0.5) * size - 1.0, vec2(0.0), size);
	vec2 last = clamp(vec2(high.x * 0.5 + 0.5, 0.5 - low.y * 0.5) * size + 1.0, vec2(0.0), size);
	if (!all(lessThan(first, last))) {
		return false;
	}
	int levels = textureQueryLevels(hzb);
	vec2 extent = last - first;
	// Mip-k texels are at least 2^k pixels wide, so at this level the rect spans at most two texels per axis.
	int level = clamp(int(ceil(log2(max(max(extent.x, extent.y), 1.0)))), 0, levels - 1);
	if (unsafe) {
		level = max(level - 1, 0);
	}
	ivec2 texelFirst = ivec2(0);
	ivec2 texelLast = ivec2(0);
	for (int attempt = 0; attempt < 16; ++attempt) {
		ivec2 mipSize = textureSize(hzb, level);
		vec2 scale = vec2(mipSize) / size;
		texelFirst = min(ivec2(floor(first * scale)), mipSize - 1);
		texelLast = min(ivec2(floor(last * scale)), mipSize - 1);
		if (unsafe || all(lessThanEqual(texelLast - texelFirst, ivec2(1))) || level >= levels - 1) {
			break;
		}
		++level;
	}
	if (unsafe) {
		texelLast = texelFirst;
	}
	if (any(greaterThan(texelLast - texelFirst, ivec2(1)))) {
		return false;
	}
	float farthest = 0.0;
	for (int y = texelFirst.y; y <= texelLast.y; ++y) {
		for (int x = texelFirst.x; x <= texelLast.x; ++x) {
			farthest = max(farthest, texelFetch(hzb, ivec2(x, y), level).r);
		}
	}
	// Strictly behind, with a few ULPs of margin near the far plane; nothing is culled against cleared depth.
	return farthest < 1.0 && nearest > farthest + 1.0e-6;
}
