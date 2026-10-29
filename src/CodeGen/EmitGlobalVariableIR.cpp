// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "EmitGlobalVariableIR.h"

#include "Base/ExprDispatcher/ExprDispatcher.h"
#include "CGModule.h"
#include "DIBuilder.h"
#include "EmitExpressionIR.h"
#include "IRBuilder.h"
#include "IRGenerator.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie {
namespace CodeGen {
class GlobalVariableGeneratorImpl : public IRGeneratorImpl {
public:
    GlobalVariableGeneratorImpl(CGModule& cgMod, const std::vector<CHIR::GlobalVar*>& chirGVs)
        : cgMod(cgMod), chirGVs(chirGVs)
    {
    }

    void EmitIR() override;

private:
    CGModule& cgMod;
    const std::vector<CHIR::GlobalVar*> chirGVs;
};

template <> class IRGenerator<GlobalVariableGeneratorImpl> : public IRGenerator<> {
public:
    IRGenerator(CGModule& cgMod, const std::vector<CHIR::GlobalVar*>& chirGVs)
        : IRGenerator<>(std::make_unique<GlobalVariableGeneratorImpl>(cgMod, chirGVs))
    {
    }
};

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
namespace {
// Zero-sized globals (e.g. Unit-typed ones) occupy no storage at all. When such
// a global happens to be the last .bss fragment after linking, its symbol
// address equals the end of the output section, so no section covers it any
// more. The debugger then cannot map the DW_OP_addr file address to a load
// address (PIE bias is applied per section) and inspecting the variable fails
// with "read memory failed".
// One 1-byte .bss guard per module is enough to fix it. The guard is internal
// and registered as llvm.used so no pass erases it.
void AppendZeroSizeGVGuard(CGModule& cgMod)
{
    auto* module = cgMod.GetLLVMModule();
    auto* guardTy = llvm::Type::getInt8Ty(cgMod.GetLLVMContext());
    auto* guard = llvm::cast<llvm::GlobalVariable>(module->getOrInsertGlobal(".cjzsguard", guardTy));
    guard->setLinkage(llvm::GlobalValue::InternalLinkage);
    guard->setAlignment(llvm::Align(1));
    guard->setInitializer(llvm::Constant::getNullValue(guardTy));
    cgMod.GetCGContext().AddLLVMUsedVars(guard->getName().str());
}
}

void GlobalVariableGeneratorImpl::EmitIR()
{
    IRBuilder2 irBuilder(cgMod);
    std::set<CHIR::GlobalVar*> quickGVs(chirGVs.begin(), chirGVs.end());
    bool needZeroSizeGuard = false;
    for (auto chirGV : cgMod.GetCGContext().GetCHIRPackage().GetGlobalVarsWithInit()) {
        auto rawGV = llvm::cast<llvm::GlobalVariable>(cgMod.GetOrInsertGlobalVariable(chirGV)->GetRawValue());
        cgMod.diBuilder->CreateGlobalVar(*chirGV);
        if (quickGVs.find(chirGV) == quickGVs.end()) {
            continue;
        }
        const auto align = cgMod.GetLLVMModule()->getDataLayout().getPrefTypeAlignment(rawGV->getType());
        rawGV->setAlignment(llvm::MaybeAlign(align));
        if (auto literal = chirGV->GetInitializer()) {
            auto literalValue = HandleLiteralValue(irBuilder, *literal);
            if (literal->GetType()->IsString()) {
                cgMod.GetCGContext().AddCJString(
                    rawGV->getName().str(), StaticCast<CHIR::StringLiteral*>(literal)->GetVal());
            } else {
                rawGV->setInitializer(llvm::cast<llvm::Constant>(literalValue));
            }
            if (chirGV->TestAttr(CHIR::Attribute::READONLY)) {
                rawGV->addAttribute(llvm::Attribute::ReadOnly);
                rawGV->setConstant(true);
            }
        } else {
            auto chirType = StaticCast<CHIR::RefType*>(chirGV->GetType())->GetBaseType();
            rawGV->setInitializer(llvm::cast<llvm::Constant>(irBuilder.CreateNullValue(*chirType)));
        }
        if (!chirGV->GetParentCustomTypeDef()) {
            auto fieldMeta = llvm::MDTuple::get(
                cgMod.GetLLVMContext(), {llvm::MDString::get(cgMod.GetLLVMContext(), MangleType(*chirGV->GetType()))});
            rawGV->setMetadata(GC_GLOBAL_VAR_TYPE, fieldMeta);
        }
        const auto& dl = cgMod.GetLLVMModule()->getDataLayout();
        // Only the last zero-initialized (i.e. .bss) global of the module can
        // end up as the final .bss fragment after linking; earlier zero-sized
        // globals are backed by the zero-initialized storage that follows them.
        // Globals with non-zero initializers (.data), string globals whose
        // initializer is set later, and constants (.rodata) do not extend .bss
        // and must not refresh this flag.
        if (!rawGV->isConstant() && rawGV->hasInitializer() && rawGV->getInitializer()->isZeroValue()) {
            needZeroSizeGuard = dl.getTypeAllocSize(rawGV->getValueType()) == 0;
        }
    }
    if (needZeroSizeGuard) {
        AppendZeroSizeGVGuard(cgMod);
    }
}
#endif

void EmitGlobalVariableIR(CGModule& cgMod, const std::vector<CHIR::GlobalVar*>& chirGVs)
{
    IRGenerator<GlobalVariableGeneratorImpl>(cgMod, chirGVs).EmitIR();
}
} // namespace CodeGen
} // namespace Cangjie
