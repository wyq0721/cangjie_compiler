// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements ICE related variables and functions.
 */

#include "cangjie/Utils/ICEUtil.h"

#include "cangjie/Basic/Version.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Frontend/CompilerInstance.h"

namespace {
using namespace Cangjie;

std::atomic<bool> g_writeOnceICEMessag(false);
} // namespace
namespace Cangjie {
namespace ICE {

int64_t TriggerPointSetter::triggerPoint = static_cast<int64_t>(Cangjie::CompileStage::COMPILE_STAGE_NUMBER);
int64_t TriggerPointSetter::interpreterTP = static_cast<int64_t>(Cangjie::CompileStage::COMPILE_STAGE_NUMBER) + 1;
int64_t TriggerPointSetter::writeCahedTP = static_cast<int64_t>(Cangjie::CompileStage::COMPILE_STAGE_NUMBER) + 2;

void TriggerPointSetter::SetICETriggerPoint(CompileStage cs)
{
    if (TriggerPointSetter::triggerPoint == Cangjie::ICE::LSP_TP) {
        return;
    }
    if (cs >= CompileStage::COMPILE_STAGE_NUMBER) {
        TriggerPointSetter::triggerPoint = static_cast<int64_t>(CompileStage::COMPILE_STAGE_NUMBER);
    } else {
        TriggerPointSetter::triggerPoint = static_cast<int64_t>(cs);
    }
}

void TriggerPointSetter::SetICETriggerPoint(int64_t tp)
{
    if (tp == Cangjie::ICE::LSP_TP) {
        TriggerPointSetter::triggerPoint = tp;
        return;
    }
    if (tp == FRONTEND_TP) {
        TriggerPointSetter::triggerPoint = static_cast<int64_t>(CompileStage::COMPILE_STAGE_NUMBER);
    } else {
        TriggerPointSetter::triggerPoint = tp;
    }
}

int64_t GetTriggerPoint()
{
    return TriggerPointSetter::triggerPoint;
}

const char* GetTriggerPointName(int64_t tp)
{
    // The following special trigger points are not part of the CompileStage enum.
    // They represent execution contexts outside the normal frontend compilation pipeline
    // that also set a trigger point for ICE reporting purposes.
    // UNITTEST_TP: set when running unit tests; ICE should not exit in this mode.
    if (tp == UNITTEST_TP) {
        return "Unit Test Runner";
    }
    // LSP_TP: set when running in language-server (LSP) mode; ICE should not exit in this mode.
    if (tp == LSP_TP) {
        return "Language Server";
    }
    // interpreterTP: set during interpreter execution (value = COMPILE_STAGE_NUMBER + 1).
    if (tp == TriggerPointSetter::interpreterTP) {
        return "Interpreter";
    }
    // writeCahedTP: set while writing cached compilation results (value = COMPILE_STAGE_NUMBER + 2).
    if (tp == TriggerPointSetter::writeCahedTP) {
        return "Write Cache";
    }
    // Any value >= COMPILE_STAGE_NUMBER that is not one of the special points above
    // means the trigger point was reset to the default (frontend/driver) context.
    if (tp >= static_cast<int64_t>(CompileStage::COMPILE_STAGE_NUMBER)) {
        return "Frontend";
    }
    // Map standard CompileStage enum values to human-readable names.
    switch (static_cast<CompileStage>(tp)) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case CompileStage::LOAD_PLUGINS:
            return "Plugin Loading";
#endif
        case CompileStage::PARSE:
            return "Parsing";
        case CompileStage::CONDITION_COMPILE:
            return "Conditional Compilation";
        case CompileStage::IMPORT_PACKAGE:
            return "Package Import";
        case CompileStage::MACRO_EXPAND:
            return "Macro Expansion";
        case CompileStage::AST_DIFF:
            return "AST Diff";
        case CompileStage::SEMA:
            return "Semantic Analysis";
        case CompileStage::DESUGAR_AFTER_SEMA:
            return "Desugar After Sema";
        case CompileStage::GENERIC_INSTANTIATION:
            return "Generic Instantiation";
        case CompileStage::OVERFLOW_STRATEGY:
            return "Overflow Strategy";
        case CompileStage::MANGLING:
            return "Mangling";
        case CompileStage::SAVE_CJO:
            return "Save CJO";
        case CompileStage::CHIR:
            return "CHIR";
        case CompileStage::CODEGEN:
            return "Code Generation";
        case CompileStage::SAVE_RESULTS:
            return "Save Results";
        default:
            return "Unknown Stage";
    }
}

bool CanWriteOnceICEMessage()
{
    return !g_writeOnceICEMessag.exchange(true);
}

void PrintVersionFromError()
{
    std::cerr << CANGJIE_COMPILER_VERSION << std::endl;
}

void RemoveTempFile()
{
    Cangjie::TempFileManager::Instance().DeleteTempFiles();
}

} // namespace ICE

} // namespace Cangjie
