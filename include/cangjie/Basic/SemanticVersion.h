// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the SemanticVersion type used for APILevel and @IfAvailable checks.
 *
 * Semantic version encoding for runtime comparison:
 *   encoded = major * 1_000_000 + minor * 1_000 + patch
 *
 * Range constraints (matching the encoding scheme):
 *   major : 0 – 999_999
 *   minor : 0 – 999
 *   patch : 0 – 999
 * Values outside these ranges produce incorrect encoded comparisons.
 */

#ifndef CANGJIE_BASIC_SEMANTICVERSION_H
#define CANGJIE_BASIC_SEMANTICVERSION_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cangjie/Utils/StdUtils.h"

namespace Cangjie {

/**
 * @brief Semantic version triple (major.minor.patch).
 *
 * Used for @!APILevel since values and --cfg APILevel_level option.
 * The sentinel value 0.0.0 means "not set" (check IsZero()).
 */
struct SemanticVersion {
    uint32_t major{0};
    uint32_t minor{0};
    uint32_t patch{0};

    SemanticVersion() = default;
    explicit SemanticVersion(uint32_t maj, uint32_t min = 0, uint32_t pat = 0)
        : major(maj), minor(min), patch(pat)
    {}

    /// Returns true when the version is the default-constructed sentinel 0.0.0.
    bool IsZero() const { return major == 0 && minor == 0 && patch == 0; }

    /// Returns "major.minor.patch" string representation.
    std::string ToString() const
    {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
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
        return static_cast<uint64_t>(major) * 1000000ULL +
               static_cast<uint64_t>(minor) * 1000ULL +
               static_cast<uint64_t>(patch);
    }

    /**
     * @brief Parses a version string of the form "major[.minor[.patch]]".
     *
     * Accepts "20", "20.1", "20.1.5". Non-numeric components default to 0.
     * Returns the zero version SemanticVersion{0,0,0} for empty or entirely
     * invalid input.
     */
    static SemanticVersion Parse(const std::string& s)
    {
        constexpr size_t versionIdxMinor = 1;
        constexpr size_t versionIdxPatch = 2;
        constexpr size_t versionMinPartsMinor = 2;
        constexpr size_t versionMinPartsPatch = 3;

        SemanticVersion version;
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
            version.major = static_cast<uint32_t>(Stoull(parts[0]).value_or(0));
        }
        if (parts.size() >= versionMinPartsMinor && !parts[versionIdxMinor].empty()) {
            version.minor = static_cast<uint32_t>(Stoull(parts[versionIdxMinor]).value_or(0));
        }
        if (parts.size() >= versionMinPartsPatch && !parts[versionIdxPatch].empty()) {
            version.patch = static_cast<uint32_t>(Stoull(parts[versionIdxPatch]).value_or(0));
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
        if (s.empty()) {
            return false;
        }
        size_t partCount = 0;
        size_t start = 0;
        while (true) {
            size_t end = s.find('.', start);
            bool lastPart = (end == std::string::npos);
            std::string part = s.substr(start, lastPart ? std::string::npos : end - start);
            if (part.empty()) {
                return false; // consecutive dots or trailing dot
            }
            for (char c : part) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            ++partCount;
            if (partCount > 3) {
                return false;
            }
            if (lastPart) {
                break;
            }
            start = end + 1;
        }
        return true;
    }

    bool operator<(const SemanticVersion& other) const
    {
        if (major != other.major) {
            return major < other.major;
        }
        if (minor != other.minor) {
            return minor < other.minor;
        }
        return patch < other.patch;
    }

    bool operator<=(const SemanticVersion& other) const { return *this < other || *this == other; }

    bool operator>(const SemanticVersion& other) const { return other < *this; }

    bool operator>=(const SemanticVersion& other) const { return !(*this < other); }

    bool operator==(const SemanticVersion& other) const
    {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    bool operator!=(const SemanticVersion& other) const { return !(*this == other); }
};

} // namespace Cangjie

#endif // CANGJIE_BASIC_SEMANTICVERSION_H
