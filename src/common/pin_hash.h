#pragma once
#include <cstdint>
#include <string>

// pin_hash - the manager PIN check (requirement 3.3 / D-05).
// The PIN itself is NEVER stored in code: only the FNV-1a 32-bit hash
// of the PIN is kept, and a typed PIN is accepted when its hash matches.
// FNV-1a: hash = 2166136261; for each byte: hash ^= byte; hash *= 16777619.
// The GUI batch wires the PIN entry to matchesManagerPin().
namespace pin_hash
{
    inline std::uint32_t fnv1a(const std::string& s)
    {
        std::uint32_t h = 2166136261u;
        for (const unsigned char c : s)
        {
            h ^= c;
            h *= 16777619u;
        }
        return h;
    }

    // kManagerPinHash is the FNV-1a 32-bit hash of the manager PIN.
    // The PIN digits themselves never appear in code, in a comment,
    // or in a test - only this hash value does.
    constexpr std::uint32_t kManagerPinHash = 0x1A98BBA5u;

    inline bool matchesManagerPin(const std::string& pin)
    {
        return fnv1a(pin) == kManagerPinHash;
    }
}
