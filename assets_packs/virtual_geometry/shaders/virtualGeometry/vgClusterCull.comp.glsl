#version 450
// GPU virtual geometry selection, pass 2 (M4-V1): one invocation per (visible instance, cluster) of one asset.
// Dispatched indirectly: X covers the asset clusters, Y is the number of visible instances appended by
// vgInstanceCull. Every invocation runs the same flat test as the CPU (SelectLodCut + CullClusters): LOD cut,
// frustum test of the cluster bounds, normal cone. Surviving clusters append one DrawIndexedIndirectCommand and one
// draw record to the segment of their (mesh, slot, material) batch; the batch counter is the count buffer of
// drawIndexedIndirectCount. Appends are aggregated per material slot in shared memory first, so a workgroup issues
// one global atomic per slot instead of one per cluster (all invocations of a group share the instance).
//
// M4-V2 two-phase occlusion, selected by VgCullView.Pass.x:
//   VG_PASS_MAIN: phase 1. A cluster of an instance with history is also tested at the previous transform against
//     the previous frame's HZB; rejected clusters go to the asset's occluded-cluster list (re-tested by
//     vgClusterCullPost) instead of being drawn. A full list draws the cluster (conservative).
//   VG_PASS_POST_INSTANCES: phase 2 for instances revived by vgInstanceCullPost: the same full test, occlusion at the
//     current transform against the current HZB; occluded clusters are dropped. Draws go to the post segments
//     (batch + VgCullView.Occlusion.y), so phase-1 and phase-2 draws never share a segment.
#define VG_CLUSTER_GROUP_SIZE 64
#include "vgClusterCullPass.glsl"
