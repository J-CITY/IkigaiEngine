#version 450
// M4-V2 phase 2, clusters: one invocation per entry of one asset's occluded-cluster list (clusters that passed LOD,
// frustum and cone in phase 1 but were rejected by the previous HZB). Only the occlusion test is repeated: current
// transform, current HZB (built from the phase-1 depth of this frame). Visible clusters append to the post segment
// of their batch. Invocations of a group belong to different instances, so appends use one global atomic each.
// Dispatched indirectly: X grows by one every 64 list appends in vgClusterCull.
#include "vgClusterCullPostPass.glsl"
