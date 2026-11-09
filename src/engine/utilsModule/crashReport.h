#pragma once

namespace IKIGAI::UTILS::CRASH {

void install() noexcept;
[[noreturn]] void terminate(const char* reason) noexcept;

}
