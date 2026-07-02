// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"

#include <optional>
#include <string>
#include <vector>

#include "cangjie/Driver/Backend/Backend.h"
#include "cangjie/Driver/Utils.h"

using namespace Cangjie;

class ToolchainTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }
};

TEST_F(ToolchainTest, Init)
{
}

// When crti.o cannot be resolved (empty optional), the OpenHarmony toolchain must produce an
// actionable diagnostic instead of letting the link fail later with a bare "cannot open crti.o".
TEST_F(ToolchainTest, DiagnoseMissingCRuntimeWarnsWhenCrtiUnresolved)
{
    const std::vector<std::string> searchedPaths{
        "/opt/ohos-sdk/native/sysroot/usr/lib/aarch64-linux-ohos", "/does/not/exist/lib"};
    std::optional<std::string> message = DiagnoseMissingCRuntime(std::nullopt, searchedPaths);

    ASSERT_TRUE(message.has_value());
    // Names the missing object so the failure is greppable.
    EXPECT_NE(message->find("cannot find the OpenHarmony C runtime object 'crti.o'"), std::string::npos);
    // Points the user at the options that populate the sysroot search paths.
    EXPECT_NE(message->find("--sysroot"), std::string::npos);
    EXPECT_NE(message->find("-B"), std::string::npos);
    EXPECT_NE(message->find("--toolchain"), std::string::npos);
    // Ties the diagnostic to the raw linker error it precedes.
    EXPECT_NE(message->find("cannot open crti.o"), std::string::npos);
    // Lists every consulted search path so the misconfiguration is obvious.
    EXPECT_NE(message->find("searched C runtime library paths"), std::string::npos);
    for (const auto& path : searchedPaths) {
        EXPECT_NE(message->find(path), std::string::npos) << "missing path in message: " << path;
    }
}

// A correctly configured sysroot resolves crti.o, so no diagnostic should be produced (no false positive).
TEST_F(ToolchainTest, DiagnoseMissingCRuntimeSilentWhenCrtiResolved)
{
    std::optional<std::string> message = DiagnoseMissingCRuntime(
        std::string{"/opt/ohos-sdk/native/sysroot/usr/lib/aarch64-linux-ohos/crti.o"},
        {"/opt/ohos-sdk/native/sysroot/usr/lib/aarch64-linux-ohos"});

    EXPECT_FALSE(message.has_value());
}

// With no search paths at all, the diagnostic still fires but omits the (empty) "searched paths" section.
TEST_F(ToolchainTest, DiagnoseMissingCRuntimeOmitsSearchedSectionWhenNoPaths)
{
    std::optional<std::string> message = DiagnoseMissingCRuntime(std::nullopt, {});

    ASSERT_TRUE(message.has_value());
    EXPECT_NE(message->find("cannot find the OpenHarmony C runtime object 'crti.o'"), std::string::npos);
    EXPECT_EQ(message->find("searched C runtime library paths"), std::string::npos);
}
