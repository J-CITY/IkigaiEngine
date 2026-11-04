#ifndef IKIGAI_TEMPORAL_COMMON_GLSL
#define IKIGAI_TEMPORAL_COMMON_GLSL
// Shared temporal filter helpers (M4-G1): history validity, disocclusion tests, variance clamp, history-length driven
// alpha, exponentially weighted variance, anti-lag and the NaN/Inf guard applied before every history write.
// Used by the probe level filter (screenProbes/spProbeTemporal) and the full resolution denoiser (giTemporal); independent
// of the screen probe settings so other effects (reflections) can include it. Needs no engine block: callers pass
// CameraParams / matrices from their own FrameData declaration.
#include "temporalDenoiseSettings.glsl"

const vec3 TD_LUMA=vec3(0.2126,0.7152,0.0722);
// Largest finite half float: history targets are fp16, larger values would store +Inf.
const float TD_HALF_MAX=65504.0;

float tdLuminance(vec3 color) { return dot(color,TD_LUMA); }

bool tdHistoryValid() { return Denoise.History.y!=0; }
float tdMaxHistory() { return float(max(Denoise.History.x,1)); }

// Exponent bits all set = Inf or NaN. Bit test instead of isnan/isinf so fast-math in a driver or DXC cannot fold it away.
bool tdFinite(float value) { return (floatBitsToUint(value)&0x7f800000u)!=0x7f800000u; }
// NaN/Inf guard of every history write: non-finite components become 0, the rest is clamped to [0, maxValue].
vec4 tdSanitize(vec4 value,float maxValue) {
    vec4 result=vec4(tdFinite(value.x)?value.x:0.0,tdFinite(value.y)?value.y:0.0,tdFinite(value.z)?value.z:0.0,
        tdFinite(value.w)?value.w:0.0);
    return clamp(result,vec4(0.0),vec4(maxValue));
}

// Linear view depth of a stored depth value (perspective). cameraParams = near, far, depth in [0,1] (1) or GL [-1,1] (0).
float tdLinearDepth(float depth,vec4 cameraParams) {
    float n=cameraParams.x,f=cameraParams.y;
    if(cameraParams.z>0.5) {
        return n*f/max(f-depth*(f-n),1e-6);
    }
    float z=depth*2.0-1.0;
    return 2.0*n*f/max((f+n)-z*(f-n),1e-6);
}
// World position of texture-space uv (cameraParams.w flips Y for VK/DX12).
vec3 tdWorldPosition(vec2 uv,float depth,mat4 invViewProjection,vec4 cameraParams) {
    uv.y=mix(uv.y,1.0-uv.y,cameraParams.w);
    vec4 h=invViewProjection*vec4(uv*2.0-1.0,mix(depth*2.0-1.0,depth,cameraParams.z),1.0);
    return h.xyz/h.w;
}

// Tolerance of a depth / plane distance test at linear depth `depth`.
float tdDepthTolerance(float depth) { return max(Denoise.Rejection.x,0.0)*depth+max(Denoise.Rejection.w,0.0); }
// Disocclusion test: the surface seen in the previous frame at the reprojected position must be the same surface (linear depth
// within tolerance, normals within NormalThreshold).
bool tdSameSurface(float expectedDepth,float previousDepth,vec3 normal,vec3 previousNormal) {
    return abs(previousDepth-expectedDepth)<=tdDepthTolerance(expectedDepth)&&dot(normal,previousNormal)>=Denoise.Rejection.y;
}

// Variance clamp: history limited to mean +- gamma * sigma of the current neighbourhood (gamma 0 disables the clamp).
vec3 tdClampToBox(vec3 history,vec3 mean,vec3 sigma,float gamma) {
    if(gamma<=0.0)return history;
    return clamp(history,mean-gamma*sigma,mean+gamma*sigma);
}
// Blend weight of the new sample: 1 / history length, so a short history converges quickly and a long one averages.
float tdHistoryAlpha(float historyLength) { return 1.0/max(historyLength,1.0); }
// Exponentially weighted variance (incremental form, no catastrophic cancellation of E[x^2] - E[x]^2 in fp16).
float tdVarianceUpdate(float variance,float mean,float value,float alpha) {
    float delta=value-mean;
    return (1.0-alpha)*(max(variance,0.0)+alpha*delta*delta);
}
// Anti-lag: when the fast luminance average leaves the noise band of the long history by more than AntiLagSigma standard
// deviations of the fast average, the history length drops to the fast window (lighting change, not a single firefly: the
// fast average already spans FastHistory frames). Returns the history length to use for this frame.
float tdAntiLag(float historyLength,float variance,float historyLuma,float fastLuma) {
    float k=Denoise.Clamp.y;
    float fastFrames=max(Denoise.Clamp.z,1.0);
    if(k<=0.0||historyLength<=fastFrames)return historyLength;
    float fastAlpha=1.0/fastFrames;
    // Standard deviation of an EMA with weight a over white noise: sigma * sqrt(a / (2 - a)); 2% relative floor.
    float sigma=sqrt(max(variance,0.0)*fastAlpha/(2.0-fastAlpha))+0.02*abs(historyLuma)+1e-5;
    return abs(fastLuma-historyLuma)>k*sigma?fastFrames:historyLength;
}
#endif
