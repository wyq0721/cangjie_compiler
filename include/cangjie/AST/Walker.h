// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Walker related classes.
 */

#ifndef CANGJIE_AST_WALKER_H
#define CANGJIE_AST_WALKER_H

#include <functional>
#include <string>
#include <utility>
#include <atomic>

#include "cangjie/AST/Node.h"

namespace Cangjie::AST {
/**
 * Enum class for visit action in the Walker.
 *
 * The values have been specially designed to make `STOP_NOW` has the highest priority when several values are combined
 * with `OR` operator.
 *
 * @see operator|(VisitAction, VisitAction)
 */
enum class VisitAction : uint8_t {
    WALK_CHILDREN, /**< Continue to walk into child items. */
    SKIP_CHILDREN, /**< Continue walking, but don't enter child items. */
    STOP_NOW,       /**< Stop walking immediately. */
    KEEP_DECISION   /**< Only clean up states. Keep action as it is. */
};

/**
 * The main class used for walking the Rune AST.
 */
template <class NodeT>
class WalkerT {
    /**
     * A typealias for the visit function callback. It accepts a pointer to the Node being visited as its only
     * parameter and returns the VisitAction after walking into it.
     */
    using VisitFunc = std::function<VisitAction(Ptr<NodeT>)>;

public:
    /**
     * The constructor to create an AST walker.
     * @param node The AST node being visited.
     * @param VisitPre The function executed before walking into its children.
     * @param VisitPost The function executed after walking into its children.
     */
    explicit WalkerT(Ptr<NodeT> node, VisitFunc VisitPre = nullptr, VisitFunc VisitPost = nullptr)
        : node(node), VisitPre(std::move(VisitPre)), VisitPost(std::move(VisitPost))
    {
        ID = GetNextWalkerID();
    }

    /**
     * The constructor to create an AST walker.
     * @param node The AST node being visited.
     * @param id Given walker id for current walker.
     * @param VisitPre The function executed before walking into its children.
     * @param VisitPost The function executed after walking into its children.
     */
    WalkerT(Ptr<NodeT> node, unsigned id, VisitFunc VisitPre = nullptr, VisitFunc VisitPost = nullptr)
        : node(node), VisitPre(std::move(VisitPre)), VisitPost(std::move(VisitPost)), ID(id)
    {
    }
    /**
     * The function starts an AST walking.
     */
    void Walk() const
    {
        Walk(node);
    };

    static unsigned GetNextWalkerID();

private:
    /**
     * The AST node as walking entry.
     */
    Ptr<NodeT> node;
    /**
     * The function executed before walking into its children.
     */
    VisitFunc VisitPre;
    /**
     * The function executed after walking into its children.
     */
    VisitFunc VisitPost;
    /**
     * Walker ID.
     */
    unsigned ID{0};
    /**
     * Next Walker ID.
     */
    static std::atomic_uint nextWalkerID;

    /**
     * The function used internally for walking a certain AST node.
     * @param curNode The node to be walked.
     * @return VisitAction decision after walking into it.
     */
    VisitAction Walk(Ptr<NodeT> curNode) const;

    /**
     * Iteratively walk a left-associative BinaryExpr chain rooted at curNode.
     * A long chain such as `a + b + ... + z` parses into a deeply left-nested
     * BinaryExpr tree; recursing once per level through Walk(leftExpr) would
     * exhaust the small Cangjie coroutine stack (see UsersForum issue 3128).
     * This replicates Walk's per-node book-keeping (visited check, VisitPre,
     * desugar walk, VisitPost) iteratively and preserves the STOP_NOW /
     * SKIP_CHILDREN semantics.
     * @param curNode The BinaryExpr node to be walked.
     * @return STOP_NOW if any visitor requested it, otherwise WALK_CHILDREN.
     */
    VisitAction WalkBinaryExprChain(Ptr<NodeT> curNode) const;

    /** True iff a VisitPost callback is registered and it requests STOP_NOW for `n`. */
    bool VisitPostRequestsStop(Ptr<NodeT> n) const
    {
        return VisitPost && VisitPost(n) == VisitAction::STOP_NOW;
    }
    template <typename T> friend class WalkerT;
};
using Walker = WalkerT<Node>;
using ConstWalker = WalkerT<const Node>;
extern template class WalkerT<Node>;
extern template class WalkerT<const Node>;
} // namespace Cangjie::AST

#endif // CANGJIE_AST_WALKER_H
