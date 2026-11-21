#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace IKIGAI::PARTICLES {
struct Definition;
}

namespace IKIGAI::RESOURCES {
std::shared_ptr<const PARTICLES::Definition> ReadParticleDefinition(std::string_view contents, const std::string& asset);
}
