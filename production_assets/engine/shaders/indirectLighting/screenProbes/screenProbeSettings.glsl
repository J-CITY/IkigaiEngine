#ifndef IKIGAI_SCREEN_PROBE_SETTINGS_GLSL
#define IKIGAI_SCREEN_PROBE_SETTINGS_GLSL
// Screen probe settings block (std140 image of ScreenProbeGpuSettings, renderGraph/screenProbeSettings.h), set 0 binding 5.
// Filled from the pipeline "ScreenProbes" block; material stages bind it with BufferLinks, ray query programs with the
// binding name "ScreenProbe".
layout(std140,set=0,binding=5) uniform ScreenProbeSettingsBlock {
    ivec4 Layout;            // TileSize, DirectionSide (directions per probe = side^2), MaxSteps, DebugMode
    vec4 Trace;              // Thickness (view units), Intensity, MissConfidence, PreviousSceneWeight
    ivec4 Filter;            // UseHistory, MaxHistory, SpatialRadius, HzbMaxLevel
    vec4 Weights;            // DepthSigma (relative plane distance), NormalPower, ClampSigma, ConfidenceHistory
    ivec4 Chain;             // ChainMask (SP_CHAIN_* bits), DdgiValid, StablePlacement, DdgiScrolling
    vec4 RayQuery;           // RayQueryMaxDistance, RayQueryConfidence, DdgiIntensity, 0
    vec4 DdgiOriginSpacing;  // DDGI grid origin xyz, probe spacing
    ivec4 DdgiCounts;        // DDGI probe counts xyz, rays per probe
} ScreenProbe;
#endif
