#pragma once

namespace IKIGAI::VG {
// AUTO selects GPU when the required features and shaders are available.
enum class VirtualGeometrySelectionPath { AUTO = 0, CPU = 1, GPU = 2 };
}
