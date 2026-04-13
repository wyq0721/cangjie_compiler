// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Semantic version related unit tests for APILevel and IfAvailable annotations.
 */

#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "cangjie/Basic/SemanticVersion.h"
#include "../../src/Sema/Plugin/PluginCustomAnnoChecker.h"
#include "cangjie/Basic/StringConvertor.h"

using namespace Cangjie;
using namespace Cangjie::PluginCheck;

// Test SemanticVersion construction
TEST(SemanticVersionTest, ConstructorTest)
{
    SemanticVersion v1;
    EXPECT_EQ(v1.major, 0);
    EXPECT_EQ(v1.minor, 0);
    EXPECT_EQ(v1.patch, 0);
    EXPECT_TRUE(v1.IsZero());

    SemanticVersion v2(20);
    EXPECT_EQ(v2.major, 20);
    EXPECT_EQ(v2.minor, 0);
    EXPECT_EQ(v2.patch, 0);
    EXPECT_FALSE(v2.IsZero());

    SemanticVersion v3(20, 1);
    EXPECT_EQ(v3.major, 20);
    EXPECT_EQ(v3.minor, 1);
    EXPECT_EQ(v3.patch, 0);

    SemanticVersion v4(20, 1, 5);
    EXPECT_EQ(v4.major, 20);
    EXPECT_EQ(v4.minor, 1);
    EXPECT_EQ(v4.patch, 5);
}

// Test parsing from string
TEST(SemanticVersionTest, ParseSimpleVersion)
{
    auto v = SemanticVersion::Parse("20");
    EXPECT_EQ(v.major, 20);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
    EXPECT_EQ(v.ToString(), "20.0.0");
}

TEST(SemanticVersionTest, ParseTwoPartVersion)
{
    auto v = SemanticVersion::Parse("20.1");
    EXPECT_EQ(v.major, 20);
    EXPECT_EQ(v.minor, 1);
    EXPECT_EQ(v.patch, 0);
    EXPECT_EQ(v.ToString(), "20.1.0");
}

TEST(SemanticVersionTest, ParseFullVersion)
{
    auto v1 = SemanticVersion::Parse("20.0.0");
    EXPECT_EQ(v1.major, 20);
    EXPECT_EQ(v1.minor, 0);
    EXPECT_EQ(v1.patch, 0);
    EXPECT_EQ(v1.ToString(), "20.0.0");

    auto v2 = SemanticVersion::Parse("20.1.5");
    EXPECT_EQ(v2.major, 20);
    EXPECT_EQ(v2.minor, 1);
    EXPECT_EQ(v2.patch, 5);
    EXPECT_EQ(v2.ToString(), "20.1.5");

    auto v3 = SemanticVersion::Parse("21.10.999");
    EXPECT_EQ(v3.major, 21);
    EXPECT_EQ(v3.minor, 10);
    EXPECT_EQ(v3.patch, 999);
    EXPECT_EQ(v3.ToString(), "21.10.999");
}

// Test equality comparison
TEST(SemanticVersionTest, EqualityComparison)
{
    SemanticVersion v1(20, 0, 0);
    SemanticVersion v2(20, 0, 0);
    SemanticVersion v3(20, 1, 0);

    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
    EXPECT_TRUE(v1 != v3);
    EXPECT_FALSE(v1 != v2);

    // Test that "20" parsed equals "20.0.0" parsed
    auto vSimple = SemanticVersion::Parse("20");
    auto vFull = SemanticVersion::Parse("20.0.0");
    EXPECT_TRUE(vSimple == vFull);
}

// Test less than comparison
TEST(SemanticVersionTest, LessThanComparison)
{
    SemanticVersion v1(19, 0, 0);
    SemanticVersion v2(20, 0, 0);
    SemanticVersion v3(20, 1, 0);
    SemanticVersion v4(20, 1, 5);
    SemanticVersion v5(21, 0, 0);

    // Major version comparison
    EXPECT_TRUE(v1 < v2);
    EXPECT_FALSE(v2 < v1);

    // Minor version comparison (same major)
    EXPECT_TRUE(v2 < v3);
    EXPECT_FALSE(v3 < v2);

    // Patch version comparison (same major and minor)
    EXPECT_TRUE(v3 < v4);
    EXPECT_FALSE(v4 < v3);

    // Mixed comparisons
    EXPECT_TRUE(v4 < v5);
    EXPECT_TRUE(v1 < v5);

    // Not less than itself
    EXPECT_FALSE(v2 < v2);
}

// Test greater than comparison
TEST(SemanticVersionTest, GreaterThanComparison)
{
    SemanticVersion v1(20, 0, 0);
    SemanticVersion v2(20, 1, 0);
    SemanticVersion v3(21, 0, 0);

    EXPECT_TRUE(v2 > v1);
    EXPECT_TRUE(v3 > v2);
    EXPECT_TRUE(v3 > v1);

    EXPECT_FALSE(v1 > v2);
    EXPECT_FALSE(v1 > v1);
}

// Test less than or equal comparison
TEST(SemanticVersionTest, LessThanOrEqualComparison)
{
    SemanticVersion v1(20, 0, 0);
    SemanticVersion v2(20, 0, 0);
    SemanticVersion v3(20, 1, 0);

    EXPECT_TRUE(v1 <= v2);  // Equal
    EXPECT_TRUE(v1 <= v3);  // Less than
    EXPECT_FALSE(v3 <= v1);
}

// Test greater than or equal comparison
TEST(SemanticVersionTest, GreaterThanOrEqualComparison)
{
    SemanticVersion v1(20, 0, 0);
    SemanticVersion v2(20, 0, 0);
    SemanticVersion v3(20, 1, 0);

    EXPECT_TRUE(v1 >= v2);  // Equal
    EXPECT_TRUE(v3 >= v1);  // Greater than
    EXPECT_FALSE(v1 >= v3);
}

// Test version ordering
TEST(SemanticVersionTest, VersionOrdering)
{
    std::vector<SemanticVersion> versions = {
        SemanticVersion::Parse("19.0.0"),
        SemanticVersion::Parse("20.0.0"),
        SemanticVersion::Parse("20.1.0"),
        SemanticVersion::Parse("20.1.5"),
        SemanticVersion::Parse("21.0.0"),
    };

    // Verify ordering
    for (size_t i = 0; i < versions.size() - 1; ++i) {
        EXPECT_TRUE(versions[i] < versions[i + 1])
            << versions[i].ToString() << " should be < " << versions[i + 1].ToString();
        EXPECT_FALSE(versions[i] > versions[i + 1]);
        EXPECT_FALSE(versions[i] == versions[i + 1]);
    }
}

// Test runtime encoding scheme
TEST(SemanticVersionTest, RuntimeEncoding)
{
    EXPECT_EQ(SemanticVersion::Parse("19.0.0").ToEncoded(), 19000000ULL);
    EXPECT_EQ(SemanticVersion::Parse("20.0.0").ToEncoded(), 20000000ULL);
    EXPECT_EQ(SemanticVersion::Parse("20.1.0").ToEncoded(), 20001000ULL);
    EXPECT_EQ(SemanticVersion::Parse("20.1.5").ToEncoded(), 20001005ULL);
    EXPECT_EQ(SemanticVersion::Parse("21.0.0").ToEncoded(), 21000000ULL);

    // Verify encoding maintains ordering
    uint64_t e1 = SemanticVersion::Parse("20.0.0").ToEncoded();
    uint64_t e2 = SemanticVersion::Parse("20.1.0").ToEncoded();
    uint64_t e3 = SemanticVersion::Parse("20.1.5").ToEncoded();
    uint64_t e4 = SemanticVersion::Parse("21.0.0").ToEncoded();

    EXPECT_LT(e1, e2);
    EXPECT_LT(e2, e3);
    EXPECT_LT(e3, e4);
}

// Test backward compatibility
TEST(SemanticVersionTest, BackwardCompatibility)
{
    // Integer literal treated as major version
    SemanticVersion vInt(20);
    auto vParsed = SemanticVersion::Parse("20");
    auto vFull = SemanticVersion::Parse("20.0.0");

    EXPECT_EQ(vInt, vParsed);
    EXPECT_EQ(vInt, vFull);
    EXPECT_EQ(vParsed, vFull);

    // All should have same encoding
    EXPECT_EQ(vInt.ToEncoded(), vParsed.ToEncoded());
    EXPECT_EQ(vInt.ToEncoded(), vFull.ToEncoded());
    EXPECT_EQ(vInt.ToEncoded(), 20000000ULL);
}

// Test IsZero method
TEST(SemanticVersionTest, IsZeroMethod)
{
    SemanticVersion v1;
    EXPECT_TRUE(v1.IsZero());

    SemanticVersion v2(0, 0, 0);
    EXPECT_TRUE(v2.IsZero());

    SemanticVersion v3(1, 0, 0);
    EXPECT_FALSE(v3.IsZero());

    SemanticVersion v4(0, 1, 0);
    EXPECT_FALSE(v4.IsZero());

    SemanticVersion v5(0, 0, 1);
    EXPECT_FALSE(v5.IsZero());
}

// Test ToString method
TEST(SemanticVersionTest, ToStringMethod)
{
    EXPECT_EQ(SemanticVersion(20, 0, 0).ToString(), "20.0.0");
    EXPECT_EQ(SemanticVersion(20, 1, 0).ToString(), "20.1.0");
    EXPECT_EQ(SemanticVersion(20, 1, 5).ToString(), "20.1.5");
    EXPECT_EQ(SemanticVersion(21, 10, 999).ToString(), "21.10.999");
}

// Test edge cases
TEST(SemanticVersionTest, EdgeCases)
{
    // Empty string
    auto v1 = SemanticVersion::Parse("");
    EXPECT_TRUE(v1.IsZero());

    // Single zero
    auto v2 = SemanticVersion::Parse("0");
    EXPECT_EQ(v2.major, 0);
    EXPECT_EQ(v2.minor, 0);
    EXPECT_EQ(v2.patch, 0);

    // Large numbers
    auto v3 = SemanticVersion::Parse("999.999.999");
    EXPECT_EQ(v3.major, 999);
    EXPECT_EQ(v3.minor, 999);
    EXPECT_EQ(v3.patch, 999);

    // Trailing dots (should be handled gracefully)
    auto v4 = SemanticVersion::Parse("20.");
    EXPECT_EQ(v4.major, 20);
    EXPECT_EQ(v4.minor, 0);
    EXPECT_EQ(v4.patch, 0);
}

// Test IsValidFormat method
TEST(SemanticVersionTest, IsValidFormat)
{
    // Valid formats
    EXPECT_TRUE(SemanticVersion::IsValidFormat("20"));
    EXPECT_TRUE(SemanticVersion::IsValidFormat("20.1"));
    EXPECT_TRUE(SemanticVersion::IsValidFormat("20.1.5"));
    EXPECT_TRUE(SemanticVersion::IsValidFormat("0.0.0"));
    EXPECT_TRUE(SemanticVersion::IsValidFormat("999.999.999"));

    // Invalid formats
    EXPECT_FALSE(SemanticVersion::IsValidFormat(""));           // empty
    EXPECT_FALSE(SemanticVersion::IsValidFormat("20."));        // trailing dot
    EXPECT_FALSE(SemanticVersion::IsValidFormat(".20"));        // leading dot
    EXPECT_FALSE(SemanticVersion::IsValidFormat("20..1"));      // consecutive dots
    EXPECT_FALSE(SemanticVersion::IsValidFormat("20.abc.1"));   // non-numeric component
    EXPECT_FALSE(SemanticVersion::IsValidFormat("20.1.2.3"));   // too many components
    EXPECT_FALSE(SemanticVersion::IsValidFormat("20.1.2a"));    // alphanumeric
}

// Test PluginCustomAnnoInfo structure
TEST(SemanticVersionTest, PluginCustomAnnoInfoIntegration)
{
    PluginCustomAnnoInfo info1;
    EXPECT_TRUE(info1.since.IsZero());

    PluginCustomAnnoInfo info2;
    info2.since = SemanticVersion(20, 1, 5);
    EXPECT_FALSE(info2.since.IsZero());
    EXPECT_EQ(info2.since.ToString(), "20.1.5");

    // Test comparison
    PluginCustomAnnoInfo info3;
    info3.since = SemanticVersion(21, 0, 0);
    EXPECT_TRUE(info2.since < info3.since);
}

// Test real-world version scenarios
TEST(SemanticVersionTest, RealWorldScenarios)
{
    // Integer API level style (major version only)
    auto api19 = SemanticVersion::Parse("19");
    auto api20 = SemanticVersion::Parse("20");
    auto api21 = SemanticVersion::Parse("21");

    EXPECT_TRUE(api19 < api20);
    EXPECT_TRUE(api20 < api21);

    // x.y.z style versioning
    auto harmony4_0_0 = SemanticVersion::Parse("4.0.0");
    auto harmony4_1_0 = SemanticVersion::Parse("4.1.0");
    auto harmony5_0_0 = SemanticVersion::Parse("5.0.0");

    EXPECT_TRUE(harmony4_0_0 < harmony4_1_0);
    EXPECT_TRUE(harmony4_1_0 < harmony5_0_0);

    // Patch version updates
    auto v1_0_0 = SemanticVersion::Parse("1.0.0");
    auto v1_0_1 = SemanticVersion::Parse("1.0.1");
    auto v1_0_10 = SemanticVersion::Parse("1.0.10");

    EXPECT_TRUE(v1_0_0 < v1_0_1);
    EXPECT_TRUE(v1_0_1 < v1_0_10);
}

// ============================================================
// Tests for --cfg APILevel scenario
//
// Simulates the CheckLevel logic triggered by --cfg APILevel_level=X.Y.Z:
//   - globalLevel  : parsed from --cfg APILevel_level (i.e., scopeLevel when no IfAvailable)
//   - targetSince  : from the @APILevel(since: "X.Y.Z") annotation on the referenced API
//   - Rule: if targetSince > scopeLevel → inaccessible (should report error)
//           if targetSince <= scopeLevel → accessible (no error)
// ============================================================

namespace {
// Helper: simulate CheckLevel return value.
// Returns true when the API is accessible (no error should be reported),
// false when the API level is too high (error should be reported).
bool IsAccessible(const SemanticVersion& targetSince, const SemanticVersion& scopeLevel)
{
    // Mirrors PluginCustomAnnoChecker::CheckLevel: error when targetSince > scopeLevel
    return !(targetSince > scopeLevel);
}
} // namespace

// --cfg APILevel_level=20 (integer style)
TEST(CfgAPILevelTest, IntegerStyleCfg_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("20");   // --cfg APILevel_level=20
    auto targetSince = SemanticVersion::Parse("20");   // @APILevel(since: "20")
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, IntegerStyleCfg_Inaccessible)
{
    auto globalLevel = SemanticVersion::Parse("19");   // --cfg APILevel_level=19
    auto targetSince = SemanticVersion::Parse("20");   // @APILevel(since: "20")
    EXPECT_FALSE(IsAccessible(targetSince, globalLevel));
}

// --cfg APILevel_level=20.0.0 (full x.y.z style)
TEST(CfgAPILevelTest, FullVersionCfg_ExactMatch_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("20.0.0");
    auto targetSince = SemanticVersion::Parse("20.0.0");
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, FullVersionCfg_HigherTarget_Inaccessible)
{
    auto globalLevel = SemanticVersion::Parse("20.0.0");  // --cfg APILevel_level=20.0.0
    auto targetSince = SemanticVersion::Parse("20.1.0");  // @APILevel(since: "20.1.0")
    EXPECT_FALSE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, FullVersionCfg_LowerTarget_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("20.1.0");  // --cfg APILevel_level=20.1.0
    auto targetSince = SemanticVersion::Parse("20.0.0");  // @APILevel(since: "20.0.0")
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

// Patch-level boundary
TEST(CfgAPILevelTest, PatchBoundary_ExactMatch_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("20.1.5");
    auto targetSince = SemanticVersion::Parse("20.1.5");
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, PatchBoundary_OneHigher_Inaccessible)
{
    auto globalLevel = SemanticVersion::Parse("20.1.5");  // --cfg APILevel_level=20.1.5
    auto targetSince = SemanticVersion::Parse("20.1.6");  // @APILevel(since: "20.1.6")
    EXPECT_FALSE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, PatchBoundary_OneLower_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("20.1.5");  // --cfg APILevel_level=20.1.5
    auto targetSince = SemanticVersion::Parse("20.1.4");  // @APILevel(since: "20.1.4")
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

// Integer --cfg is equivalent to x.0.0
TEST(CfgAPILevelTest, IntegerEquivalentToMajorZeroZero)
{
    auto cfgInt = SemanticVersion::Parse("20");      // --cfg APILevel_level=20
    auto cfgFull = SemanticVersion::Parse("20.0.0"); // --cfg APILevel_level=20.0.0

    // Both forms must yield the same API availability decisions
    auto targetBelow = SemanticVersion::Parse("19.9.9");
    auto targetEqual = SemanticVersion::Parse("20.0.0");
    auto targetAbove = SemanticVersion::Parse("20.0.1");

    EXPECT_EQ(IsAccessible(targetBelow, cfgInt), IsAccessible(targetBelow, cfgFull));
    EXPECT_EQ(IsAccessible(targetEqual, cfgInt), IsAccessible(targetEqual, cfgFull));
    EXPECT_EQ(IsAccessible(targetAbove, cfgInt), IsAccessible(targetAbove, cfgFull));
}

// Two-part --cfg (e.g., --cfg APILevel_level=20.1)
TEST(CfgAPILevelTest, TwoPartCfg_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("20.1");    // --cfg APILevel_level=20.1
    auto targetSince = SemanticVersion::Parse("20.1.0");  // @APILevel(since: "20.1.0")
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, TwoPartCfg_Inaccessible)
{
    auto globalLevel = SemanticVersion::Parse("20.1");    // --cfg APILevel_level=20.1
    auto targetSince = SemanticVersion::Parse("20.2.0");  // @APILevel(since: "20.2.0")
    EXPECT_FALSE(IsAccessible(targetSince, globalLevel));
}

// Cross-major boundary
TEST(CfgAPILevelTest, CrossMajor_OlderCfg_Inaccessible)
{
    auto globalLevel = SemanticVersion::Parse("20.9.9");  // --cfg APILevel_level=20.9.9
    auto targetSince = SemanticVersion::Parse("21.0.0");  // @APILevel(since: "21.0.0")
    EXPECT_FALSE(IsAccessible(targetSince, globalLevel));
}

TEST(CfgAPILevelTest, CrossMajor_NewerCfg_Accessible)
{
    auto globalLevel = SemanticVersion::Parse("21.0.0");  // --cfg APILevel_level=21.0.0
    auto targetSince = SemanticVersion::Parse("20.9.9");  // @APILevel(since: "20.9.9")
    EXPECT_TRUE(IsAccessible(targetSince, globalLevel));
}

// No --cfg provided: globalLevel is zero, optionWithLevel==false (CheckLevel returns true immediately).
// Modeled here as: zero globalLevel should never block access via the IsZero guard.
TEST(CfgAPILevelTest, NoCfgProvided_GlobalLevelIsZero)
{
    SemanticVersion globalLevel;  // default-constructed = 0.0.0 (IsZero)
    EXPECT_TRUE(globalLevel.IsZero());
    // When optionWithLevel is false, CheckLevel returns true regardless; ensure zero is detectable.
    auto targetSince = SemanticVersion::Parse("21.0.0");
    // The real CheckLevel would skip the check; here we verify the zero sentinel.
    EXPECT_FALSE(targetSince.IsZero());
}

// Batch API availability: multiple APIs at different levels with a fixed --cfg
TEST(CfgAPILevelTest, BatchAccessibility_FixedCfg)
{
    auto globalLevel = SemanticVersion::Parse("20.1.5");  // --cfg APILevel_level=20.1.5

    // accessible APIs (since <= 20.1.5)
    EXPECT_TRUE(IsAccessible(SemanticVersion::Parse("19.0.0"), globalLevel));
    EXPECT_TRUE(IsAccessible(SemanticVersion::Parse("20.0.0"), globalLevel));
    EXPECT_TRUE(IsAccessible(SemanticVersion::Parse("20.1.0"), globalLevel));
    EXPECT_TRUE(IsAccessible(SemanticVersion::Parse("20.1.5"), globalLevel));

    // inaccessible APIs (since > 20.1.5)
    EXPECT_FALSE(IsAccessible(SemanticVersion::Parse("20.1.6"), globalLevel));
    EXPECT_FALSE(IsAccessible(SemanticVersion::Parse("20.2.0"), globalLevel));
    EXPECT_FALSE(IsAccessible(SemanticVersion::Parse("21.0.0"), globalLevel));
}
