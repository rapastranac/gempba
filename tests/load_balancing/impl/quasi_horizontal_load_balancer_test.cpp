/*
 * MIT License
 *
 * Copyright (c) 2024. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <gtest/gtest.h>

#include "load_balancing/impl/quasi_horizontal_load_balancer.hpp"
#include "function_trace/factory/trace_node_factory.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-31
 */

class QuasiHorizontalLoadBalancerTest : public ::testing::Test {
protected:
protected:

    void SetUp() override {
        balancer = gempba::QuasiHorizontalLoadBalancer::getInstance();
    }

    void TearDown() override {
        balancer->reset();
    }

    gempba::QuasiHorizontalLoadBalancer *balancer{};
};


TEST_F(QuasiHorizontalLoadBalancerTest, SetGetRoot) {

    std::shared_ptr<gempba::TraceNode> root1 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, nullptr);
    std::shared_ptr<gempba::TraceNode> root2 = gempba::TraceNodeFactory::createNode<int>(*balancer, 2, nullptr);
    std::shared_ptr<gempba::TraceNode> root3 = gempba::TraceNodeFactory::createNode<int>(*balancer, 3, nullptr);

    balancer->setRoot(root1->getThreadId(), root1.get());
    balancer->setRoot(root2->getThreadId(), root2.get());
    balancer->setRoot(root3->getThreadId(), root3.get());

    ASSERT_EQ(root1.get(), *balancer->getRoot(1));
    ASSERT_EQ(root2.get(), *balancer->getRoot(2));
    ASSERT_EQ(root3.get(), *balancer->getRoot(3));

}

TEST_F(QuasiHorizontalLoadBalancerTest, Prune) {

    std::shared_ptr<gempba::TraceNode> parent = gempba::TraceNodeFactory::createDummy(*balancer, 1);
    std::shared_ptr<gempba::TraceNode> child1 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, parent.get());
    std::shared_ptr<gempba::TraceNode> child2 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, parent.get());
    std::shared_ptr<gempba::TraceNode> child3 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, parent.get());

    ASSERT_EQ(parent.get(), child1->getParent());
    ASSERT_EQ(parent.get(), child2->getParent());
    ASSERT_EQ(parent.get(), child3->getParent());

    ASSERT_EQ(parent.get(), *child1->getRoot());
    ASSERT_EQ(parent.get(), *child2->getRoot());
    ASSERT_EQ(parent.get(), *child3->getRoot());


    balancer->prune(*child1);
    ASSERT_EQ(nullptr, child1->getParent());
    ASSERT_EQ(nullptr, child1->getRoot());

    balancer->prune(*child2);
    ASSERT_EQ(nullptr, child2->getParent());
    ASSERT_EQ(nullptr, child2->getRoot());

    balancer->prune(*child3);
    ASSERT_EQ(nullptr, child3->getParent());
    ASSERT_EQ(nullptr, child3->getRoot());
}

TEST_F(QuasiHorizontalLoadBalancerTest, GetUniqueId) {
    ASSERT_EQ(1, balancer->getUniqueId());
    ASSERT_EQ(2, balancer->getUniqueId());
    ASSERT_EQ(3, balancer->getUniqueId());
    ASSERT_EQ(4, balancer->getUniqueId());
}

TEST_F(QuasiHorizontalLoadBalancerTest, GetAccumulateIdleTime) {

    ASSERT_EQ(0, balancer->getIdleTime());
    balancer->accumulateIdleTime(10);
    ASSERT_EQ(10, balancer->getIdleTime());
    balancer->accumulateIdleTime(20);
    ASSERT_EQ(30, balancer->getIdleTime());
}

TEST_F(QuasiHorizontalLoadBalancerTest, Reset) {

    std::shared_ptr<gempba::TraceNode> dummy1 = gempba::TraceNodeFactory::createDummy(*balancer, 1);
    std::shared_ptr<gempba::TraceNode> dummy2 = gempba::TraceNodeFactory::createDummy(*balancer, 2);
    std::shared_ptr<gempba::TraceNode> dummy3 = gempba::TraceNodeFactory::createDummy(*balancer, 3);
    std::shared_ptr<gempba::TraceNode> dummy4 = gempba::TraceNodeFactory::createDummy(*balancer, 4);

    balancer->setRoot(dummy1->getThreadId(), dummy1.get());
    balancer->setRoot(dummy2->getThreadId(), dummy2.get());
    balancer->setRoot(dummy3->getThreadId(), dummy3.get());
    balancer->setRoot(dummy4->getThreadId(), dummy4.get());

    ASSERT_EQ(1, dummy1->getThreadId());
    ASSERT_EQ(2, dummy2->getThreadId());
    ASSERT_EQ(3, dummy3->getThreadId());
    ASSERT_EQ(4, dummy4->getThreadId());

    balancer->accumulateIdleTime(10);
    ASSERT_EQ(10, balancer->getIdleTime());
    balancer->accumulateIdleTime(10);
    ASSERT_EQ(20, balancer->getIdleTime());
    balancer->accumulateIdleTime(10);
    ASSERT_EQ(30, balancer->getIdleTime());

    balancer->reset();

    ASSERT_EQ(0, balancer->getIdleTime());
    ASSERT_EQ(nullptr, balancer->getRoot(1));
    ASSERT_EQ(nullptr, balancer->getRoot(2));
    ASSERT_EQ(nullptr, balancer->getRoot(3));
    ASSERT_EQ(nullptr, balancer->getRoot(4));

    ASSERT_EQ(1, balancer->getUniqueId());
}

TEST_F(QuasiHorizontalLoadBalancerTest, pruneLeftSibling) {
    // create a dummy parent
    std::shared_ptr<gempba::TraceNode> parent = gempba::TraceNodeFactory::createDummy(*balancer, 1);
    std::shared_ptr<gempba::TraceNode> child1 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, parent.get());
    std::shared_ptr<gempba::TraceNode> child2 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, parent.get());
    std::shared_ptr<gempba::TraceNode> child3 = gempba::TraceNodeFactory::createNode<int>(*balancer, 1, parent.get());

    ASSERT_EQ(3, parent->getChildrenCount());

    balancer->pruneLeftSibling(*child3);

    ASSERT_EQ(2, parent->getChildrenCount());
    //child1 should be pruned
    ASSERT_EQ(nullptr, child1->getParent());
    ASSERT_EQ(nullptr, child1->getRoot());

    balancer->pruneLeftSibling(*child3);

    // As child3 is the only remaining child, it becomes the new root
    // therefore the parent should be pruned
    ASSERT_EQ(0, parent->getChildrenCount());
    ASSERT_EQ(nullptr, parent->getParent());
    ASSERT_EQ(nullptr, parent->getRoot());
    //child2 should be pruned
    ASSERT_EQ(nullptr, child2->getParent());
    ASSERT_EQ(nullptr, child2->getRoot());

    //child3 should be the new root
    ASSERT_EQ(child3.get(), *child3->getRoot());
    ASSERT_EQ(nullptr, child3->getParent());

    balancer->pruneLeftSibling(*child3); // exits

}

TEST_F(QuasiHorizontalLoadBalancerTest, FindTopTraceNode) {
    std::shared_ptr<gempba::TraceNode> dummy = gempba::TraceNodeFactory::createDummy(*balancer, 1);

    gempba::TraceNode *topTraceNode = balancer->findTopTraceNode(*dummy);
    ASSERT_EQ(nullptr, topTraceNode);

    std::shared_ptr<gempba::TraceNode> child1 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    topTraceNode = balancer->findTopTraceNode(*child1);
    ASSERT_EQ(nullptr, topTraceNode);


    /**
     * From this point onwards:
     * @code
     *           root != dummy
     *            |  \  \   \
     *            |   \    \    \
     *            |    \      \     \
     *            |     \        \      \
     *         child<sub>1</sub>  child<sub>2</sub>  child<sub>3</sub>  child<sub>4</sub>
     *            |
     *       grandChild<sub>1</sub>    <-- this level
     *
     * @endcode
     *
     */

    std::shared_ptr<gempba::TraceNode> child2 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> child3 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> child4 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());

    std::shared_ptr<gempba::TraceNode> grandChild1 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, child1.get());

    topTraceNode = balancer->findTopTraceNode(*grandChild1);
    ASSERT_EQ(child2.get(), topTraceNode);

    topTraceNode = balancer->findTopTraceNode(*grandChild1);
    ASSERT_EQ(child3.get(), topTraceNode);

    topTraceNode = balancer->findTopTraceNode(*grandChild1);
    ASSERT_EQ(child4.get(), topTraceNode);

    //grandChild1 should become the new root
    ASSERT_EQ(grandChild1.get(), *grandChild1->getRoot());
    ASSERT_EQ(nullptr, grandChild1->getParent());
}

TEST_F(QuasiHorizontalLoadBalancerTest, MaybePruneLeftSiblingFirstLevel) {
    std::shared_ptr<gempba::TraceNode> dummy = gempba::TraceNodeFactory::createDummy(*balancer, 1);

    // first level
    std::shared_ptr<gempba::TraceNode> node11 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> node12 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> node13 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> node14 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());

    /**
     *  @code
     *                dummy
     *             /   | \   \
     *           /     |   \    \
     *          /      |      \     \
     *         /       |        \     \
     *        node<sub>1,1</sub>  node<sub>1,2</sub> node <sub>1,3</sub> node<sub>1,4</sub>
     *
     * @endcode
     */

    {
        //nothing changes, because child1 is the first child
        balancer->maybePruneLeftSibling(*node11);
        ASSERT_EQ(4, dummy->getChildrenCount());
        auto it = dummy->getChildren().begin();
        ASSERT_EQ(node11.get(), *it);
        ASSERT_EQ(node12.get(), *(++it));
        ASSERT_EQ(node13.get(), *(++it));
        ASSERT_EQ(node14.get(), *(++it));
    }

    {
        balancer->maybePruneLeftSibling(*node12);
        ASSERT_EQ(nullptr, node11->getParent());
        ASSERT_EQ(nullptr, node11->getRoot());

        ASSERT_EQ(3, dummy->getChildrenCount());
        auto it = dummy->getChildren().begin();
        ASSERT_EQ(node12.get(), *it);
        ASSERT_EQ(node13.get(), *(++it));
        ASSERT_EQ(node14.get(), *(++it));
    }

    {
        balancer->maybePruneLeftSibling(*node13);
        ASSERT_EQ(nullptr, node12->getParent());
        ASSERT_EQ(nullptr, node12->getRoot());

        ASSERT_EQ(2, dummy->getChildrenCount());
        auto it = dummy->getChildren().begin();
        ASSERT_EQ(node13.get(), *it);
        ASSERT_EQ(node14.get(), *(++it));
    }

    {
        // here node14 becomes the new root as it is the last child
        balancer->maybePruneLeftSibling(*node14);
        ASSERT_EQ(nullptr, node13->getParent());
        ASSERT_EQ(nullptr, node13->getRoot());

        ASSERT_EQ(0, dummy->getChildrenCount());
        ASSERT_EQ(nullptr, node14->getParent());
        ASSERT_EQ(node14.get(), *node14->getRoot());
    }
}

TEST_F(QuasiHorizontalLoadBalancerTest, MaybePruneLeftSiblingOtherLevel) {
    std::shared_ptr<gempba::TraceNode> dummy = gempba::TraceNodeFactory::createDummy(*balancer, 1);

    // first level
    std::shared_ptr<gempba::TraceNode> child1 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> child2 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());

    // second level
    std::shared_ptr<gempba::TraceNode> child1Child1 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, child1.get());
    std::shared_ptr<gempba::TraceNode> child1Child2 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, child1.get());

    // third level
    std::shared_ptr<gempba::TraceNode> child1Child1Child1 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, child1Child1.get());
    std::shared_ptr<gempba::TraceNode> child1Child1Child2 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, child1Child1.get());

    {
        //nothing changes, because child1Child1Child1 is the first child
        balancer->maybePruneLeftSibling(*child1Child1Child1);
        ASSERT_EQ(2, child1Child1->getChildrenCount());
        auto it = child1Child1->getChildren().begin();
        ASSERT_EQ(child1Child1Child1.get(), *it);
        ASSERT_EQ(child1Child1Child2.get(), *(++it));
    }

    {
        balancer->maybePruneLeftSibling(*child1Child1Child2);
        ASSERT_EQ(1, child1Child1->getChildrenCount());
        ASSERT_EQ(child1Child1Child2.get(), child1Child1->getFirstChild());
    }
}

TEST_F(QuasiHorizontalLoadBalancerTest, MaybePruneLeftSiblingOtherLevelLoweringRoot1) {

    std::shared_ptr<gempba::TraceNode> dummy = gempba::TraceNodeFactory::createDummy(*balancer, 1);

    // first level
    std::shared_ptr<gempba::TraceNode> node11 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> node12 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());

    // second level
    std::shared_ptr<gempba::TraceNode> node21 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node12.get());
    std::shared_ptr<gempba::TraceNode> node22 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node12.get());

    // third level
    std::shared_ptr<gempba::TraceNode> node31 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node21.get());
    std::shared_ptr<gempba::TraceNode> node32 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node21.get());

    //assert that the root of all nodes is dummy
    ASSERT_EQ(dummy.get(), *node11->getRoot());
    ASSERT_EQ(dummy.get(), *node12->getRoot());
    ASSERT_EQ(dummy.get(), *node21->getRoot());
    ASSERT_EQ(dummy.get(), *node22->getRoot());
    ASSERT_EQ(dummy.get(), *node31->getRoot());
    ASSERT_EQ(dummy.get(), *node32->getRoot());


    /**
     * Given the following structure:
     * @code
     *          dummy
     *          /  |
     *     node<sub>1,1</sub>  node<sub>1,2</sub>
     *             |    \
     *           node<sub>2,1</sub>  node<sub>2,2</sub>
     *           /    \
     *        node<sub>3,1</sub>  node<sub>3,2</sub>
     *
     * @endcode
     *
     * When we call <code>maybePruneLeftSibling(node12)</code> then, <code>node<sub>1,2</sub></code> ends up without siblings, therefore
     * it should become the new root.
     */

    balancer->maybePruneLeftSibling(*node12);
    ASSERT_EQ(nullptr, dummy->getParent());
    ASSERT_EQ(nullptr, dummy->getRoot());
    ASSERT_EQ(0, dummy->getChildrenCount());

    ASSERT_EQ(nullptr, node11->getParent());
    ASSERT_EQ(nullptr, node11->getRoot());

    //assert that the root of all nodes is node12
    ASSERT_EQ(node12.get(), *node12->getRoot());
    ASSERT_EQ(node12.get(), *node21->getRoot());
    ASSERT_EQ(node12.get(), *node22->getRoot());
    ASSERT_EQ(node12.get(), *node31->getRoot());
    ASSERT_EQ(node12.get(), *node32->getRoot());
}

TEST_F(QuasiHorizontalLoadBalancerTest, MaybePruneLeftSiblingOtherLevelLoweringRoot2) {

    std::shared_ptr<gempba::TraceNode> dummy = gempba::TraceNodeFactory::createDummy(*balancer, 1);

    // first level
    std::shared_ptr<gempba::TraceNode> node11 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());
    std::shared_ptr<gempba::TraceNode> node12 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, dummy.get());

    // second level
    std::shared_ptr<gempba::TraceNode> node21 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node12.get());

    // third level
    std::shared_ptr<gempba::TraceNode> node31 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node21.get());

    // fourth level
    std::shared_ptr<gempba::TraceNode> node41 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node31.get());
    std::shared_ptr<gempba::TraceNode> node42 = gempba::TraceNodeFactory::createNode<>(*balancer, 1, node31.get());


    //assert that the root of all nodes is dummy
    ASSERT_EQ(dummy.get(), *node11->getRoot());
    ASSERT_EQ(dummy.get(), *node12->getRoot());
    ASSERT_EQ(dummy.get(), *node21->getRoot());
    ASSERT_EQ(dummy.get(), *node31->getRoot());
    ASSERT_EQ(dummy.get(), *node41->getRoot());
    ASSERT_EQ(dummy.get(), *node42->getRoot());


    /**
     * Given the following structure:
     * @code
     *          dummy
     *          /  |
     *     node<sub>1,1</sub>  node<sub>1,2</sub>
     *             |
     *           node<sub>2,1</sub>
     *             |
     *           node<sub>3,1</sub>
     *           /    \
     *        node<sub>4,1</sub>  node<sub>4,2</sub>
     *
     * @endcode
     *
     * When we call <code>maybePruneLeftSibling(node12)</code> then, <code>node<sub>1,2</sub></code> ends up without siblings, therefore
     * a new root needs to be found, which is <code>node<sub>3,1</sub></code>, because it is first node front top-to-bottom that has at least two children
     */

    balancer->maybePruneLeftSibling(*node12);
    ASSERT_EQ(nullptr, dummy->getParent());
    ASSERT_EQ(nullptr, dummy->getRoot());
    ASSERT_EQ(0, dummy->getChildrenCount());

    ASSERT_EQ(nullptr, node11->getParent());
    ASSERT_EQ(nullptr, node11->getRoot());
    ASSERT_EQ(0, node11->getChildrenCount());

    ASSERT_EQ(nullptr, node12->getParent());
    ASSERT_EQ(nullptr, node12->getRoot());
    ASSERT_EQ(0, node12->getChildrenCount());

    ASSERT_EQ(nullptr, node21->getParent());
    ASSERT_EQ(nullptr, node21->getRoot());
    ASSERT_EQ(0, node21->getChildrenCount());

    //assert that the root of all nodes is node31
    ASSERT_EQ(nullptr, node31->getParent());
    ASSERT_EQ(node31.get(), *node31->getRoot());
    ASSERT_EQ(node31.get(), *node41->getRoot());
    ASSERT_EQ(node31.get(), *node42->getRoot());
}

