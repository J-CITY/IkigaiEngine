#version 450
// One candidate leaf range per group. Each lane runs the original flat cluster predicate.
#define VG_HIERARCHY_CANDIDATES
#define VG_CLUSTER_GROUP_SIZE 8
#include "vgClusterCullPass.glsl"
