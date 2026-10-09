#include <cstdint>
#include <cstddef>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE

// Polyfill for __hash_memory which is missing in iOS < 17 but required by libc++ in C++20 mode
// when using _LIBCPP_DISABLE_AVAILABILITY.
namespace std {
namespace __1 {
    size_t __hash_memory(const void* p, size_t n);
    size_t __hash_memory(const void* p, size_t n) {
        // Simple FNV-1a hash
#if defined(__LP64__)
        size_t hash = 14695981039346656037ull;
        const uint8_t* ptr = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            hash ^= ptr[i];
            hash *= 1099511628211ull;
        }
        return hash;
#else
        size_t hash = 2166136261u;
        const uint8_t* ptr = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            hash ^= ptr[i];
            hash *= 16777619u;
        }
        return hash;
#endif
    }
}
}

#endif
#endif
