// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares MemberSignature struct.
 */

#ifndef CANGJIE_SEMA_MEMBER_SIGNATURE_H
#define CANGJIE_SEMA_MEMBER_SIGNATURE_H

#include "cangjie/AST/Node.h"
#include "cangjie/AST/ASTCasting.h"

namespace Cangjie {

struct MemberSignature {
    Ptr<AST::Decl> decl = nullptr;
    AST::DataTy ty = nullptr;
    AST::DataTy structTy = nullptr;
    ModalInfo thisMode;
    /*
     * If the member is a member of another visible extension, it points to the extension declaration.
     * Otherwise, it is null.
     */
    Ptr<AST::ExtendDecl> extendDecl = nullptr;
    std::vector<std::unordered_set<AST::DataTy>> upperBounds;
    /*
     * List of the corresponding types came from super-types which are inconsistent.
     */
    std::unordered_set<AST::ModalTy> inconsistentTypes;
    /*
     * True: if this member has multiple default implementation.
     */
    bool shouldBeImplemented = false;
    /*
     * True: if this member override others.
     */
    bool replaceOther = false;
    /*
     * True: if current member is implementing inherited interface decl.
     */
    bool isInheritedInterface = false;
};

using MemberMap = std::multimap<std::string, MemberSignature>;

/**
 * Cheap pre-check before comparing function signatures: substituting generic types never changes the number of
 * parameters, so two function types with different parameter counts can never have identical parameter types.
 * Returns true when either type is not a function type, leaving that case to the caller's full check.
 */
inline bool HaveSameParamCount(AST::DataTy ty1, AST::DataTy ty2)
{
    auto funcTy1 = DynamicCast<AST::FuncTy>(ty1);
    auto funcTy2 = DynamicCast<AST::FuncTy>(ty2);
    return !funcTy1 || !funcTy2 || funcTy1->paramTys.size() == funcTy2->paramTys.size();
}

} // namespace Cangjie

#endif
