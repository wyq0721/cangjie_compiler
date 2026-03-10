// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Unit tests for ICEUtil: verifies that GetTriggerPointName correctly maps
 * every CompileStage enum value and every special trigger-point constant to
 * its expected human-readable stage name used in ICE messages.
 */

#include "gtest/gtest.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/ICEUtil.h"

using namespace Cangjie;
using namespace Cangjie::ICE;

// ---------------------------------------------------------------------------
// Tests for standard CompileStage enum values
// ---------------------------------------------------------------------------

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
TEST(GetTriggerPointNameTest, LoadPluginsStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::LOAD_PLUGINS)), "Plugin Loading");
}
#endif

TEST(GetTriggerPointNameTest, ParseStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::PARSE)), "Parsing");
}

TEST(GetTriggerPointNameTest, ConditionCompileStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::CONDITION_COMPILE)), "Conditional Compilation");
}

TEST(GetTriggerPointNameTest, ImportPackageStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::IMPORT_PACKAGE)), "Package Import");
}

TEST(GetTriggerPointNameTest, MacroExpandStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::MACRO_EXPAND)), "Macro Expansion");
}

TEST(GetTriggerPointNameTest, AstDiffStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::AST_DIFF)), "AST Diff");
}

TEST(GetTriggerPointNameTest, SemaStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::SEMA)), "Semantic Analysis");
}

TEST(GetTriggerPointNameTest, DesugarAfterSemaStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::DESUGAR_AFTER_SEMA)), "Desugar After Sema");
}

TEST(GetTriggerPointNameTest, GenericInstantiationStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::GENERIC_INSTANTIATION)), "Generic Instantiation");
}

TEST(GetTriggerPointNameTest, OverflowStrategyStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::OVERFLOW_STRATEGY)), "Overflow Strategy");
}

TEST(GetTriggerPointNameTest, ManglingStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::MANGLING)), "Mangling");
}

TEST(GetTriggerPointNameTest, SaveCJOStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::SAVE_CJO)), "Save CJO");
}

TEST(GetTriggerPointNameTest, CHIRStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::CHIR)), "CHIR");
}

TEST(GetTriggerPointNameTest, CodegenStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::CODEGEN)), "Code Generation");
}

TEST(GetTriggerPointNameTest, SaveResultsStage)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::SAVE_RESULTS)), "Save Results");
}

// ---------------------------------------------------------------------------
// Tests for special trigger-point constants (outside CompileStage enum)
// ---------------------------------------------------------------------------

// COMPILE_STAGE_NUMBER itself (and any larger unrecognised value) maps to "Frontend"
TEST(GetTriggerPointNameTest, FrontendStageFromCompileStageNumber)
{
    EXPECT_STREQ(GetTriggerPointName(static_cast<int64_t>(CompileStage::COMPILE_STAGE_NUMBER)), "Frontend");
}

// interpreterTP = COMPILE_STAGE_NUMBER + 1
TEST(GetTriggerPointNameTest, InterpreterStage)
{
    EXPECT_STREQ(GetTriggerPointName(TriggerPointSetter::interpreterTP), "Interpreter");
}

// writeCahedTP = COMPILE_STAGE_NUMBER + 2
TEST(GetTriggerPointNameTest, WriteCacheStage)
{
    EXPECT_STREQ(GetTriggerPointName(TriggerPointSetter::writeCahedTP), "Write Cache");
}

// UNITTEST_TP (-2): ICE does not exit the process in this mode
TEST(GetTriggerPointNameTest, UnitTestRunnerStage)
{
    EXPECT_STREQ(GetTriggerPointName(UNITTEST_TP), "Unit Test Runner");
}

// LSP_TP (-3): ICE does not exit the process in this mode
TEST(GetTriggerPointNameTest, LanguageServerStage)
{
    EXPECT_STREQ(GetTriggerPointName(LSP_TP), "Language Server");
}

// ---------------------------------------------------------------------------
// Tests that verify the assembled ICE message suffix is human-readable
// ---------------------------------------------------------------------------

// The message part two must contain "Error occurred at stage: "
TEST(ICEMessageTest, MsgPartTwoText)
{
    EXPECT_NE(MSG_PART_TWO.find("Error occurred at stage: "), std::string::npos);
}

// Full suffix for every standard stage must not contain any digits,
// confirming that numeric error codes have been eliminated.
TEST(ICEMessageTest, NoNumericCodeInStageSuffix)
{
    const std::vector<CompileStage> stages = {
        CompileStage::PARSE,
        CompileStage::CONDITION_COMPILE,
        CompileStage::IMPORT_PACKAGE,
        CompileStage::MACRO_EXPAND,
        CompileStage::AST_DIFF,
        CompileStage::SEMA,
        CompileStage::DESUGAR_AFTER_SEMA,
        CompileStage::GENERIC_INSTANTIATION,
        CompileStage::OVERFLOW_STRATEGY,
        CompileStage::MANGLING,
        CompileStage::SAVE_CJO,
        CompileStage::CHIR,
        CompileStage::CODEGEN,
        CompileStage::SAVE_RESULTS,
    };
    for (CompileStage stage : stages) {
        std::string suffix = MSG_PART_TWO + GetTriggerPointName(static_cast<int64_t>(stage));
        // The suffix must mention a stage name, not a bare number
        EXPECT_NE(suffix.find("Error occurred at stage: "), std::string::npos)
            << "MSG_PART_TWO changed unexpectedly";
        bool hasDigit = false;
        for (char c : std::string(GetTriggerPointName(static_cast<int64_t>(stage)))) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                hasDigit = true;
                break;
            }
        }
        EXPECT_FALSE(hasDigit)
            << "Stage name for CompileStage " << static_cast<int64_t>(stage)
            << " should not contain digits: " << GetTriggerPointName(static_cast<int64_t>(stage));
    }
}
