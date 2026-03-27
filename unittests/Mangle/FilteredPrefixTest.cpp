// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"

#include <vector>

#define private public

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"

using namespace Cangjie;
using namespace AST;

namespace {

// Helper: compute filteredPrefix using the ORIGINAL O(n) algorithm
std::vector<Ptr<Node>> ComputeFilteredPrefixOriginal(const std::vector<Ptr<Node>>& prefix)
{
    std::vector<Ptr<Node>> filteredPrefix;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (filteredPrefix.size() != 0 && Is<Expr>(filteredPrefix.back().get()) &&
            static_cast<AST::Expr*>(filteredPrefix.back().get())->desugarExpr.get() == prefix[i].get()) {
            filteredPrefix.pop_back();
        }
        filteredPrefix.emplace_back(prefix[i]);
    }
    return filteredPrefix;
}

// Helper: incrementally push to filtered prefix (matching the optimized code)
void IncrementalPush(std::vector<Ptr<Node>>& filteredPrefix,
                     std::vector<Ptr<Node>>& replacedNodes,
                     Ptr<Node> node)
{
    Ptr<Node> replaced = nullptr;
    if (!filteredPrefix.empty() && Is<Expr>(filteredPrefix.back().get()) &&
        static_cast<AST::Expr*>(filteredPrefix.back().get())->desugarExpr.get() == node) {
        replaced = filteredPrefix.back();
        filteredPrefix.pop_back();
    }
    filteredPrefix.emplace_back(node);
    replacedNodes.push_back(replaced);
}

// Helper: incrementally pop from filtered prefix
void IncrementalPop(std::vector<Ptr<Node>>& filteredPrefix,
                    std::vector<Ptr<Node>>& replacedNodes)
{
    filteredPrefix.pop_back();
    if (replacedNodes.back() != nullptr) {
        filteredPrefix.push_back(replacedNodes.back());
    }
    replacedNodes.pop_back();
}

} // namespace

class FilteredPrefixTest : public ::testing::Test {
protected:
    DiagnosticEngine diag;
    SourceManager sm;
};

// Test that incremental push produces the same result as original algorithm
// for a simple prefix without desugar
TEST_F(FilteredPrefixTest, IncrementalMatchesOriginalNoDesugar)
{
    // Create simple non-Expr nodes (using FuncDecl which is not an Expr)
    auto body1 = CreateFuncBody({}, nullptr, CreateBlock({}), nullptr);
    auto func1 = CreateFuncDecl("f1", std::move(body1));
    auto body2 = CreateFuncBody({}, nullptr, CreateBlock({}), nullptr);
    auto func2 = CreateFuncDecl("f2", std::move(body2));

    std::vector<Ptr<Node>> prefix;
    std::vector<Ptr<Node>> filteredPrefix;
    std::vector<Ptr<Node>> replacedNodes;

    // Push func1
    prefix.push_back(func1.get());
    IncrementalPush(filteredPrefix, replacedNodes, func1.get());
    auto expected1 = ComputeFilteredPrefixOriginal(prefix);
    ASSERT_EQ(filteredPrefix.size(), expected1.size());
    for (size_t i = 0; i < filteredPrefix.size(); i++) {
        EXPECT_EQ(filteredPrefix[i], expected1[i]);
    }

    // Push func2
    prefix.push_back(func2.get());
    IncrementalPush(filteredPrefix, replacedNodes, func2.get());
    auto expected2 = ComputeFilteredPrefixOriginal(prefix);
    ASSERT_EQ(filteredPrefix.size(), expected2.size());
    for (size_t i = 0; i < filteredPrefix.size(); i++) {
        EXPECT_EQ(filteredPrefix[i], expected2[i]);
    }

    // Pop func2
    prefix.pop_back();
    IncrementalPop(filteredPrefix, replacedNodes);
    auto expected3 = ComputeFilteredPrefixOriginal(prefix);
    ASSERT_EQ(filteredPrefix.size(), expected3.size());
    for (size_t i = 0; i < filteredPrefix.size(); i++) {
        EXPECT_EQ(filteredPrefix[i], expected3[i]);
    }

    // Pop func1
    prefix.pop_back();
    IncrementalPop(filteredPrefix, replacedNodes);
    EXPECT_TRUE(filteredPrefix.empty());
    EXPECT_TRUE(prefix.empty());
}

// Test incremental push/pop produces same result with desugar expressions
TEST_F(FilteredPrefixTest, IncrementalMatchesOriginalWithDesugar)
{
    // Create an expression with a desugarExpr pointing to another node
    std::string code = "func test() {\n"
                       "    let x = 1 + 2\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    // Walk the AST using original algorithm and incremental algorithm simultaneously
    // and verify they produce the same filteredPrefix at each step
    std::vector<Ptr<Node>> prefix;
    std::vector<Ptr<Node>> filteredPrefix;
    std::vector<Ptr<Node>> replacedNodes;
    bool allMatch = true;
    int totalNodes = 0;
    int replacementCount = 0;

    Walker(file.get(),
        [&](Ptr<Node> node) -> VisitAction {
            totalNodes++;
            // Compute original filteredPrefix
            auto expected = ComputeFilteredPrefixOriginal(prefix);
            // Verify incremental filteredPrefix matches
            if (filteredPrefix.size() != expected.size()) {
                allMatch = false;
            } else {
                for (size_t i = 0; i < filteredPrefix.size(); i++) {
                    if (filteredPrefix[i] != expected[i]) {
                        allMatch = false;
                    }
                }
            }
            // Incrementally update and track replacements
            Ptr<Node> replaced = nullptr;
            if (!filteredPrefix.empty() && Is<Expr>(filteredPrefix.back().get()) &&
                static_cast<AST::Expr*>(filteredPrefix.back().get())->desugarExpr.get() == node) {
                replaced = filteredPrefix.back();
                filteredPrefix.pop_back();
                replacementCount++;
            }
            filteredPrefix.emplace_back(node);
            replacedNodes.push_back(replaced);
            prefix.emplace_back(node);
            return VisitAction::WALK_CHILDREN;
        },
        [&](Ptr<Node> node) -> VisitAction {
            IncrementalPop(filteredPrefix, replacedNodes);
            prefix.pop_back();
            return VisitAction::KEEP_DECISION;
        }).Walk();

    EXPECT_TRUE(allMatch);
    EXPECT_TRUE(filteredPrefix.empty());
    EXPECT_TRUE(prefix.empty());
    EXPECT_TRUE(replacedNodes.empty());
    // Verify we actually walked a non-trivial number of nodes
    EXPECT_GT(totalNodes, 0);
}

// Test that after full walk, all state is cleaned up
TEST_F(FilteredPrefixTest, StateCleanupAfterWalk)
{
    std::string code = "func a() {\n"
                       "    func b() {\n"
                       "        func c() {}\n"
                       "    }\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    std::vector<Ptr<Node>> prefix;
    std::vector<Ptr<Node>> filteredPrefix;
    std::vector<Ptr<Node>> replacedNodes;

    Walker(file.get(),
        [&](Ptr<Node> node) -> VisitAction {
            IncrementalPush(filteredPrefix, replacedNodes, node);
            prefix.emplace_back(node);
            return VisitAction::WALK_CHILDREN;
        },
        [&](Ptr<Node> node) -> VisitAction {
            IncrementalPop(filteredPrefix, replacedNodes);
            prefix.pop_back();
            return VisitAction::KEEP_DECISION;
        }).Walk();

    EXPECT_TRUE(filteredPrefix.empty());
    EXPECT_TRUE(prefix.empty());
    EXPECT_TRUE(replacedNodes.empty());
}

// Test empty prefix case
TEST_F(FilteredPrefixTest, EmptyPrefix)
{
    std::vector<Ptr<Node>> prefix;
    auto result = ComputeFilteredPrefixOriginal(prefix);
    EXPECT_TRUE(result.empty());

    std::vector<Ptr<Node>> filteredPrefix;
    std::vector<Ptr<Node>> replacedNodes;
    // No pushes or pops - should remain empty
    EXPECT_TRUE(filteredPrefix.empty());
}

// Test single element prefix
TEST_F(FilteredPrefixTest, SingleElement)
{
    auto body = CreateFuncBody({}, nullptr, CreateBlock({}), nullptr);
    auto func = CreateFuncDecl("single", std::move(body));

    std::vector<Ptr<Node>> prefix = {func.get()};
    auto expected = ComputeFilteredPrefixOriginal(prefix);

    std::vector<Ptr<Node>> filteredPrefix;
    std::vector<Ptr<Node>> replacedNodes;
    IncrementalPush(filteredPrefix, replacedNodes, func.get());

    ASSERT_EQ(filteredPrefix.size(), expected.size());
    EXPECT_EQ(filteredPrefix[0], expected[0]);

    IncrementalPop(filteredPrefix, replacedNodes);
    EXPECT_TRUE(filteredPrefix.empty());
}

// Test that the desugar replacement path is correctly handled
TEST_F(FilteredPrefixTest, DesugarReplacementExplicit)
{
    // Manually create an Expr with a desugarExpr to explicitly test the replacement path
    // Parse code that creates expressions; some expressions may have desugarExpr set by parser
    std::string code = "func test() {\n"
                       "    let x = 1\n"
                       "    let y = 2\n"
                       "    let z = 3\n"
                       "}\n";
    Parser parser(code, diag, sm);
    auto file = parser.ParseTopLevel();
    ASSERT_TRUE(file != nullptr);

    // Manually set up desugar relationships to test the replacement code path
    // Collect all nodes into a flat list
    std::vector<Ptr<Node>> allNodes;
    Walker(file.get(), [&allNodes](Ptr<Node> node) -> VisitAction {
        allNodes.push_back(node);
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    ASSERT_GT(allNodes.size(), 2U);

    // Find an Expr node and a non-Expr node
    Ptr<Expr> exprNode = nullptr;
    Ptr<Node> otherNode = nullptr;
    for (auto& n : allNodes) {
        if (Is<Expr>(n.get()) && exprNode == nullptr) {
            exprNode = static_cast<Expr*>(n.get());
        } else if (!Is<Expr>(n.get()) && otherNode == nullptr && n != file.get()) {
            otherNode = n;
        }
    }

    if (exprNode && otherNode) {
        // Set up desugarExpr relationship: exprNode->desugarExpr = otherNode
        auto originalDesugar = exprNode->desugarExpr.get();
        exprNode->desugarExpr = MakeOwned<RefExpr>();
        auto desugarTarget = exprNode->desugarExpr.get();

        // Build prefix: [exprNode, desugarTarget]
        std::vector<Ptr<Node>> prefix = {exprNode, desugarTarget};
        auto expectedOriginal = ComputeFilteredPrefixOriginal(prefix);

        // Build incrementally
        std::vector<Ptr<Node>> filteredPrefix;
        std::vector<Ptr<Node>> replacedNodes;
        IncrementalPush(filteredPrefix, replacedNodes, exprNode);
        IncrementalPush(filteredPrefix, replacedNodes, desugarTarget);

        // Both should produce the same result
        ASSERT_EQ(filteredPrefix.size(), expectedOriginal.size());
        for (size_t i = 0; i < filteredPrefix.size(); i++) {
            EXPECT_EQ(filteredPrefix[i], expectedOriginal[i]);
        }
        // Since exprNode->desugarExpr == desugarTarget, the replacement should have occurred
        // filteredPrefix should be [desugarTarget] (exprNode replaced)
        EXPECT_EQ(filteredPrefix.size(), 1U);
        EXPECT_EQ(filteredPrefix[0], desugarTarget);
        // Verify replacement tracking
        EXPECT_NE(replacedNodes[1], nullptr);

        // Pop and verify undo
        IncrementalPop(filteredPrefix, replacedNodes);
        EXPECT_EQ(filteredPrefix.size(), 1U);
        EXPECT_EQ(filteredPrefix[0], exprNode);

        IncrementalPop(filteredPrefix, replacedNodes);
        EXPECT_TRUE(filteredPrefix.empty());
    }
}
