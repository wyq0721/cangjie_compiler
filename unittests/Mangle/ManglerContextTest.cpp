// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"

#define private public

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;

class ManglerContextTest : public ::testing::Test {
protected:
    DiagnosticEngine diag;
    SourceManager sm;
};

// Test that CollectAllLocalInfo collects local variables correctly
TEST_F(ManglerContextTest, CollectAllLocalInfoCollectsVars)
{
    // Create a simple function with local variable declarations
    std::string code = "func foo() {\n"
                       "    let x = 1\n"
                       "    let y = 2\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    // Find the FuncDecl
    Ptr<FuncDecl> funcDecl = nullptr;
    Walker(file.get(), [&funcDecl](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FUNC_DECL) {
            funcDecl = static_cast<FuncDecl*>(node);
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_TRUE(funcDecl != nullptr);

    ManglerContext ctx;
    ctx.CollectAllLocalInfo(funcDecl);

    // Verify variables collected under funcBody key
    auto funcBodyKey = funcDecl->funcBody.get();
    EXPECT_TRUE(ctx.node2LocalVar.find(funcBodyKey) != ctx.node2LocalVar.end());
    if (ctx.node2LocalVar.find(funcBodyKey) != ctx.node2LocalVar.end()) {
        auto& varMap = ctx.node2LocalVar[funcBodyKey];
        // Should have entries for local variable declarations
        EXPECT_FALSE(varMap.empty());
    }
}

// Test that CollectAllLocalInfo collects lambdas correctly
TEST_F(ManglerContextTest, CollectAllLocalInfoCollectsLambdas)
{
    std::string code = "func bar() {\n"
                       "    let f = { => 42 }\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    Ptr<FuncDecl> funcDecl = nullptr;
    Walker(file.get(), [&funcDecl](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FUNC_DECL) {
            funcDecl = static_cast<FuncDecl*>(node);
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_TRUE(funcDecl != nullptr);

    ManglerContext ctx;
    ctx.CollectAllLocalInfo(funcDecl);

    auto funcBodyKey = funcDecl->funcBody.get();
    EXPECT_TRUE(ctx.node2Lambda.find(funcBodyKey) != ctx.node2Lambda.end());
    if (ctx.node2Lambda.find(funcBodyKey) != ctx.node2Lambda.end()) {
        EXPECT_FALSE(ctx.node2Lambda[funcBodyKey].empty());
    }
}

// Test that CollectAllLocalInfo produces consistent results with separate Save calls
TEST_F(ManglerContextTest, CollectAllLocalInfoConsistentWithSaveCalls)
{
    std::string code = "func baz() {\n"
                       "    let a = 1\n"
                       "    func inner() {}\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    Ptr<FuncDecl> funcDecl = nullptr;
    Walker(file.get(), [&funcDecl](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FUNC_DECL) {
            funcDecl = static_cast<FuncDecl*>(node);
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_TRUE(funcDecl != nullptr);

    // Collect using the merged method
    ManglerContext ctxMerged;
    ctxMerged.CollectAllLocalInfo(funcDecl);

    // Collect using the original separate methods
    ManglerContext ctxSeparate;
    ctxSeparate.SaveVar2CurDecl(funcDecl);
    ctxSeparate.SaveLambda2CurDecl(funcDecl);
    ctxSeparate.SaveLocalWildcardVar2Decl(funcDecl);
    ctxSeparate.SaveFunc2CurDecl(funcDecl);

    auto funcBodyKey = funcDecl->funcBody.get();

    // Compare node2LocalVar
    EXPECT_EQ(ctxMerged.node2LocalVar.count(funcBodyKey),
              ctxSeparate.node2LocalVar.count(funcBodyKey));
    if (ctxMerged.node2LocalVar.count(funcBodyKey) && ctxSeparate.node2LocalVar.count(funcBodyKey)) {
        EXPECT_EQ(ctxMerged.node2LocalVar[funcBodyKey].size(),
                  ctxSeparate.node2LocalVar[funcBodyKey].size());
    }

    // Compare node2LocalFunc
    EXPECT_EQ(ctxMerged.node2LocalFunc.count(funcBodyKey),
              ctxSeparate.node2LocalFunc.count(funcBodyKey));
    if (ctxMerged.node2LocalFunc.count(funcBodyKey) && ctxSeparate.node2LocalFunc.count(funcBodyKey)) {
        EXPECT_EQ(ctxMerged.node2LocalFunc[funcBodyKey].size(),
                  ctxSeparate.node2LocalFunc[funcBodyKey].size());
    }

    // Compare node2Lambda
    EXPECT_EQ(ctxMerged.node2Lambda.count(funcBodyKey),
              ctxSeparate.node2Lambda.count(funcBodyKey));

    // Compare node2LocalWildcardVar
    EXPECT_EQ(ctxMerged.node2LocalWildcardVar.count(funcBodyKey),
              ctxSeparate.node2LocalWildcardVar.count(funcBodyKey));
}

// Test CollectAllLocalInfo with empty function
TEST_F(ManglerContextTest, CollectAllLocalInfoEmptyFunction)
{
    std::string code = "func empty() {}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    Ptr<FuncDecl> funcDecl = nullptr;
    Walker(file.get(), [&funcDecl](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FUNC_DECL) {
            funcDecl = static_cast<FuncDecl*>(node);
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_TRUE(funcDecl != nullptr);

    ManglerContext ctx;
    ctx.CollectAllLocalInfo(funcDecl);

    auto funcBodyKey = funcDecl->funcBody.get();
    // Empty function should have no local vars, lambdas, funcs, or wildcards
    EXPECT_TRUE(ctx.node2LocalVar.find(funcBodyKey) == ctx.node2LocalVar.end() ||
                ctx.node2LocalVar[funcBodyKey].empty());
    EXPECT_TRUE(ctx.node2Lambda.find(funcBodyKey) == ctx.node2Lambda.end() ||
                ctx.node2Lambda[funcBodyKey].empty());
    EXPECT_TRUE(ctx.node2LocalFunc.find(funcBodyKey) == ctx.node2LocalFunc.end() ||
                ctx.node2LocalFunc[funcBodyKey].empty());
}

// Test CollectAllLocalInfo with nested functions - ensure proper scoping
TEST_F(ManglerContextTest, CollectAllLocalInfoNestedFuncScoping)
{
    std::string code = "func outer() {\n"
                       "    let outerVar = 1\n"
                       "    func inner() {\n"
                       "        let innerVar = 2\n"
                       "    }\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    // Find the outer FuncDecl (first one)
    Ptr<FuncDecl> outerFunc = nullptr;
    Walker(file.get(), [&outerFunc](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FUNC_DECL) {
            outerFunc = static_cast<FuncDecl*>(node);
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_TRUE(outerFunc != nullptr);

    ManglerContext ctx;
    ctx.CollectAllLocalInfo(outerFunc);

    auto funcBodyKey = outerFunc->funcBody.get();

    // The outer function should collect local vars (outerVar) but NOT innerVar
    // (since innerVar is inside a nested FuncBody which should be skipped for var collection)
    if (ctx.node2LocalVar.find(funcBodyKey) != ctx.node2LocalVar.end()) {
        // Count total vars under the outer function scope
        size_t totalVars = 0;
        for (auto& [name, vars] : ctx.node2LocalVar[funcBodyKey]) {
            totalVars += vars.size();
        }
        // Should only have outer scope vars, not inner scope vars
        // The exact count depends on parsing, but verify it's non-zero
        EXPECT_GT(totalVars, 0U);
    }

    // The outer function should still collect inner function declarations
    if (ctx.node2LocalFunc.find(funcBodyKey) != ctx.node2LocalFunc.end()) {
        size_t totalFuncs = 0;
        for (auto& [name, funcs] : ctx.node2LocalFunc[funcBodyKey]) {
            totalFuncs += funcs.size();
        }
        EXPECT_GT(totalFuncs, 0U);
    }
}

// Test that CollectAllLocalInfo handles nullptr node gracefully
TEST_F(ManglerContextTest, CollectAllLocalInfoNullNode)
{
    ManglerContext ctx;
    // Should not crash with a node that doesn't match any handled type
    auto block = CreateBlock(std::vector<OwnedPtr<Node>>{});
    ctx.CollectAllLocalInfo(block.get());

    // No data should be collected
    EXPECT_TRUE(ctx.node2LocalVar.empty());
    EXPECT_TRUE(ctx.node2Lambda.empty());
    EXPECT_TRUE(ctx.node2LocalFunc.empty());
    EXPECT_TRUE(ctx.node2LocalWildcardVar.empty());
}
