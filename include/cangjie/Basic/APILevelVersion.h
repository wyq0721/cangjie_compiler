// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the APILevelVersion type used for @!APILevel and @IfAvailable checks.
 *
 * API level version encoding for runtime comparison:
 *   encoded = major * 1_000_000 + minor * 1_000 + patch
 *
 * Range constraints (matching the encoding scheme):
 *   major : 0 – 999_999
 *   minor : 0 – 999
 *   patch : 0 – 999
 * Values outside these ranges produce incorrect encoded comparisons.
 */

#ifndef CANGJIE_BASIC_APILEVELVERSION_H
#define CANGJIE_BASIC_APILEVELVERSION_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cangjie/Utils/StdUtils.h"

namespace Cangjie {

/**
 * @brief API level version triple (major.minor.patch).
 *
 * Used for @!APILevel since values and --cfg APILevel_level option.
 * The sentinel value 0.0.0 means "not set" (check IsZero()).
 */
struct APILevelVersion {
    uint32_t Major{0};
    uint32_t Minor{0};
    uint32_t Patch{0};

    APILevelVersion() = default;
    explicit APILevelVersion(uint32_t maj, uint32_t min = 0, uint32_t pat = 0)
        : Major(maj), Minor(min), Patch(pat)
    {}

    /// Returns true when the version is the default-constructed sentinel 0.0.0.
    bool IsZero() const { return Major == 0 && Minor == 0 && Patch == 0; }

    /// Returns "major.minor.patch" string representation.
    std::string ToString() const
    {
        return std::to_string(Major) + "." + std::to_string(Minor) + "." + std::to_string(Patch);
    }

    /**
     * @brief Returns a compact display string, omitting trailing zero components.
     *
     * This preserves backward compatibility with integer-style API levels:
     *   {10, 0, 0} -> "10"
     *   {10, 1, 0} -> "10.1"
     *   {10, 1, 5} -> "10.1.5"
     * Used for diagnostic messages so existing test golden output is unchanged.
     */
    std::string ToDisplayString() const
    {
        if (Patch != 0) {
            return ToString();
        }
        if (Minor != 0) {
            return std::to_string(Major) + "." + std::to_string(Minor);
        }
        return std::to_string(Major);
    }

    /**
     * @brief Encodes the version as a single uint64_t suitable for runtime comparison.
     *
     * Encoding: major * 1_000_000 + minor * 1_000 + patch
     * This ordering guarantees that encoded(v1) < encoded(v2) iff v1 < v2,
     * provided minor and patch are each in [0, 999].
     */
    uint64_t ToEncoded() const
    {
        return static_cast<uint64_t>(Major) * 1000000ULL +
               static_cast<uint64_t>(Minor) * 1000ULL +
               static_cast<uint64_t>(Patch);
    }

    /**
     * @brief Parses a version string of the form "major[.minor[.patch]]".
     *
     * Accepts "20", "20.1", "20.1.5". Non-numeric components default to 0.
     * Returns the zero version APILevelVersion{0,0,0} for empty or entirely
     * invalid input.
     */
    static APILevelVersion Parse(const std::string& s)
    {
        constexpr size_t versionIdxMinor = 1;
        constexpr size_t versionIdxPatch = 2;
        constexpr size_t versionMinPartsMinor = 2;
        constexpr size_t versionMinPartsPatch = 3;

        APILevelVersion version;
        if (s.empty()) {
            return version;
        }

        std::vector<std::string> parts;
        size_t start = 0;
        size_t end = s.find('.');
        while (end != std::string::npos) {
            parts.push_back(s.substr(start, end - start));
            start = end + 1;
            end = s.find('.', start);
        }
        parts.push_back(s.substr(start));

        if (!parts[0].empty()) {
            version.Major = static_cast<uint32_t>(Stoull(parts[0]).value_or(0));
        }
        if (parts.size() >= versionMinPartsMinor && !parts[versionIdxMinor].empty()) {
            version.Minor = static_cast<uint32_t>(Stoull(parts[versionIdxMinor]).value_or(0));
        }
        if (parts.size() >= versionMinPartsPatch && !parts[versionIdxPatch].empty()) {
            version.Patch = static_cast<uint32_t>(Stoull(parts[versionIdxPatch]).value_or(0));
        }

        return version;
    }

    /**
     * @brief Validates that @p s is a well-formed version string.
     *
     * A valid string contains 1–3 dot-separated components, each consisting
     * solely of decimal digits (no leading zeros required, no alphabetics).
     */
    static bool IsValidFormat(const std::string& s)
    {
        constexpr size_t MAX_VERSION_PARTS = 3;
        if (s.empty()) {
            return false;
        }
        size_t partCount = 0;
        size_t pos = 0;
        const size_t len = s.size();
        while (pos < len) {
            size_t dotPos = s.find('.', pos);
            size_t partEnd = (dotPos == std::string::npos) ? len : dotPos;
            if (partEnd == pos) {
                return false; // empty component (leading or consecutive dot)
            }
            for (size_t i = pos; i < partEnd; ++i) {
                if (s[i] < '0' || s[i] > '9') {
                    return false;
                }
            }
            ++partCount;
            if (partCount > MAX_VERSION_PARTS) {
                return false;
            }
            if (dotPos == std::string::npos) {
                pos = len; // last part processed, exit loop
            } else {
                pos = dotPos + 1;
                if (pos == len) {
                    return false; // trailing dot (e.g., "1.2.")
                }
            }
        }
        return partCount > 0;
    }

    bool operator<(const APILevelVersion& other) const
    {
        if (Major != other.Major) {
            return Major < other.Major;
        }
        if (Minor != other.Minor) {
            return Minor < other.Minor;
        }
        return Patch < other.Patch;
    }

    bool operator<=(const APILevelVersion& other) const { return *this < other || *this == other; }

    bool operator>(const APILevelVersion& other) const { return other < *this; }

    bool operator>=(const APILevelVersion& other) const { return !(*this < other); }

    bool operator==(const APILevelVersion& other) const
    {
        return Major == other.Major && Minor == other.Minor && Patch == other.Patch;
    }

    bool operator!=(const APILevelVersion& other) const { return !(*this == other); }
};

} // namespace Cangjie

#endif // CANGJIE_BASIC_APILEVELVERSION_H
