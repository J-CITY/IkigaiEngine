#ifndef IKIGAI_TEMPORAL_DENOISE_SETTINGS_GLSL
#define IKIGAI_TEMPORAL_DENOISE_SETTINGS_GLSL
// Temporal denoiser settings block (std140 image of TemporalDenoiseGpuSettings,
// renderModule/indirectLighting/temporalDenoiseSettings.h), set 0 binding 6. One block per pipeline "TemporalDenoisers" entry;
// stages bind it with BufferLinks { "Denoise": "<entry Name>" }. History.y is rewritten every frame from the per-effect
// history validity (HistoryValidity, M3-1): 0 after a camera cut, resize, pipeline reload, light / environment change.
layout(std140,set=0,binding=6) uniform TemporalDenoiseSettingsBlock {
    ivec4 History;    // MaxHistory, HistoryValid, ClampStride (texels between clamp taps), Effect
    vec4 Rejection;   // DepthTolerance (relative linear depth), NormalThreshold (cosine), ConfidenceHistory, DepthBias (view units)
    vec4 Clamp;       // ClampSigma, AntiLagSigma (0 = off), FastHistory (frames of the fast luminance average), 0
} Denoise;
#endif
