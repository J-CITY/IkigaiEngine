// Shared traversal/indirect ABI. State per asset and phase: candidate count, overflow bits, visits used, spare.
const uint VG_HIERARCHY_QUEUE_OVERFLOW = 1u;
const uint VG_HIERARCHY_VISIT_OVERFLOW = 2u;
// Vulkan's guaranteed minimum and DirectX 12's limit for one dispatch dimension.
const uint VG_HIERARCHY_DISPATCH_WIDTH = 65535u;
