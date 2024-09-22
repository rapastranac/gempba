#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../../../GemPBA/load_balancing/api/load_balancer.hpp"
#include "../../../../GemPBA/function_trace/factory/trace_node_factory.hpp"

#include<vector>
#include <future>

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */

class LoadBalancerMock : public gempba::LoadBalancer {
public:
    MOCK_METHOD(int, getUniqueId, (), (override));
    MOCK_METHOD(long long, getIdleTime, (), (const override));
    MOCK_METHOD(void, accumulateIdleTime, (long nanoseconds), (override));
    MOCK_METHOD(void, setRoot, (int threadId, gempba::TraceNode * root), (override));
    MOCK_METHOD(gempba::TraceNode * *, getRoot, (int threadId), (override));
    MOCK_METHOD(gempba::TraceNode *, findTopTraceNode, (gempba::TraceNode & node), (override));
    MOCK_METHOD(void, maybePruneLeftSibling, (gempba::TraceNode & node), (override));
    MOCK_METHOD(void, pruneLeftSibling, (gempba::TraceNode & node), (override));
    MOCK_METHOD(void, prune, (gempba::TraceNode & node), (override));
    MOCK_METHOD(void, reset, (), (override));
};

class MyStruct {

    int value;
public:

    MyStruct() : value(0) {}

    explicit MyStruct(int value) : value(value) {}

    MyStruct(const MyStruct &other) = default;

    MyStruct(MyStruct &&other) noexcept: value(std::exchange(other.value, 0)) {}

    bool operator==(const MyStruct &other) const {
        return value == other.value;
    }

    MyStruct &operator=(MyStruct &&other) noexcept {
        if (this != &other) {
            value = std::exchange(other.value, 0);
        }
        return *this;
    }

};

template<typename ...Args>
void foo(gempba::AbstractTraceNode<Args...> *node) {
    MyStruct ins{7};
    float f1 = 1.0f;
    double d1 = 3.0;
    std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    node->setArguments(ins, f1, d1, vec);
}

class TraceNodeTest : public ::testing::Test {
protected:
    LoadBalancerMock balancerMock;
    std::map<int, void *> roots;
    int uniqueId = 0;

    void SetUp() override {
        EXPECT_CALL(balancerMock, getUniqueId())
                .WillRepeatedly([this]() { return ++uniqueId; });
        EXPECT_CALL(balancerMock, setRoot(testing::_, testing::_))
                .WillRepeatedly([this](int id, gempba::TraceNode *node) { roots[id] = static_cast<void *>(node); });
        EXPECT_CALL(balancerMock, getRoot(testing::_))
                .WillRepeatedly([this](int id) { return reinterpret_cast<gempba::TraceNode **>(&roots[id]); });
    }

    void TearDown() override {
        roots.clear();
    }

};

TEST_F(TraceNodeTest, SetGetArgumentsNode) {
    auto traceNode = gempba::TraceNodeFactory::createNode<MyStruct, float, double, std::vector<float>>(balancerMock, -1, nullptr);
    auto node = dynamic_cast<gempba::AbstractTraceNode<MyStruct, float, double, std::vector<float>> *>(traceNode.get());

    foo(node);

    auto &args = node->getArgumentsRef();

    auto &object = std::get<0>(args);
    auto f = std::get<1>(args);
    auto d = std::get<2>(args);
    auto vec = std::get<3>(args);

    auto expStruct = MyStruct{7};
    ASSERT_EQ(expStruct, object);
    ASSERT_FLOAT_EQ(1.0f, f);
    ASSERT_DOUBLE_EQ(3.0, d);

    std::vector<float> expVec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    ASSERT_EQ(10, vec.size());
    ASSERT_EQ(expVec, vec);
}

TEST_F(TraceNodeTest, GetPointersDummyNode) {
    auto node = gempba::TraceNodeFactory::createDummy(balancerMock, -1);

    ASSERT_TRUE(node->isDummy());
    ASSERT_FALSE(node->getParent());
    ASSERT_TRUE(node->getRoot());
    ASSERT_EQ(*(node->getRoot()), node->getItself());
    ASSERT_EQ(0, node->getChildrenCount());

}

TEST_F(TraceNodeTest, GetPointersNonDummyNode) {
    auto node = gempba::TraceNodeFactory::createNode<MyStruct, float, double, std::vector<float>>(balancerMock, -1, nullptr);

    ASSERT_FALSE(node->isDummy());
    ASSERT_FALSE(node->getParent());
    ASSERT_TRUE(node->getRoot());
    ASSERT_EQ(*(node->getRoot()), node->getItself());
    ASSERT_EQ(0, node->getChildrenCount());
}

TEST_F(TraceNodeTest, ThreeLevelNodesWithDummyNode) {
    auto parent = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */

    auto n11 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());
    auto n21 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, n11.get());
    auto n22 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, n11.get());

    auto n12 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());
    auto n23 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, n12.get());
    auto n24 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, n12.get());

    ASSERT_EQ(2, parent->getChildrenCount());
    ASSERT_EQ(parent.get(), n11->getParent());
    ASSERT_EQ(parent.get(), n12->getParent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11->getChildrenCount());
    ASSERT_EQ(n11.get(), n21->getParent());
    ASSERT_EQ(n11.get(), n22->getParent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12->getChildrenCount());
    ASSERT_EQ(n12.get(), n23->getParent());
    ASSERT_EQ(n12.get(), n24->getParent());

    // all nodes should have the same root
    ASSERT_EQ(*(n11->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n21->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n22->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n12->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n23->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n24->getRoot()), reinterpret_cast<void *>(parent.get()));

}

TEST_F(TraceNodeTest, SetRootAndParents) {
    auto parent = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */

    try {
        parent->setParent(parent.get());
        FAIL();
    } catch (std::exception &e) {};

    // The following lines should not be instantiated in practice, yet this is only for testing purposes
    auto n11 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 1, nullptr);
    auto n21 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 2, nullptr);
    auto n22 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 3, nullptr);

    auto n12 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 4, nullptr);
    auto n23 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 5, nullptr);
    auto n24 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 6, nullptr);

    // Every node should be its own root
    ASSERT_EQ(*(n11->getRoot()), reinterpret_cast<void *>(n11.get()));
    ASSERT_EQ(*(n21->getRoot()), reinterpret_cast<void *>(n21.get()));
    ASSERT_EQ(*(n22->getRoot()), reinterpret_cast<void *>(n22.get()));
    ASSERT_EQ(*(n12->getRoot()), reinterpret_cast<void *>(n12.get()));
    ASSERT_EQ(*(n23->getRoot()), reinterpret_cast<void *>(n23.get()));
    ASSERT_EQ(*(n24->getRoot()), reinterpret_cast<void *>(n24.get()));

    n11->setParent(parent.get());
    n12->setParent(parent.get());

    n21->setParent(n11.get());
    n22->setParent(n11.get());
    n23->setParent(n12.get());
    n24->setParent(n12.get());

    ASSERT_EQ(2, parent->getChildrenCount());
    ASSERT_EQ(parent.get(), n11->getParent());
    ASSERT_EQ(parent.get(), n12->getParent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11->getChildrenCount());
    ASSERT_EQ(n11.get(), n21->getParent());
    ASSERT_EQ(n11.get(), n22->getParent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12->getChildrenCount());
    ASSERT_EQ(n12.get(), n23->getParent());
    ASSERT_EQ(n12.get(), n24->getParent());

    // all nodes should have the same root
    ASSERT_EQ(*(n11->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n21->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n22->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n12->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n23->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n24->getRoot()), reinterpret_cast<void *>(parent.get()));

}

TEST_F(TraceNodeTest, SetRootAndChildren) {
    auto parent = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */
    try {
        parent->setParent(parent.get());
        FAIL();
    } catch (std::exception &e) {};

    // The following lines should not be instantiated in practice, yet this is only for testing purposes
    auto n11 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 1, nullptr);
    auto n21 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 2, nullptr);
    auto n22 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 3, nullptr);

    auto n12 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 4, nullptr);
    auto n23 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 5, nullptr);
    auto n24 = gempba::TraceNodeFactory::createNode<int>(balancerMock, 6, nullptr);

    // Every node should be its own root
    ASSERT_EQ(*(n11->getRoot()), reinterpret_cast<void *>(n11.get()));
    ASSERT_EQ(*(n21->getRoot()), reinterpret_cast<void *>(n21.get()));
    ASSERT_EQ(*(n22->getRoot()), reinterpret_cast<void *>(n22.get()));
    ASSERT_EQ(*(n12->getRoot()), reinterpret_cast<void *>(n12.get()));
    ASSERT_EQ(*(n23->getRoot()), reinterpret_cast<void *>(n23.get()));
    ASSERT_EQ(*(n24->getRoot()), reinterpret_cast<void *>(n24.get()));


    parent->addChild(n11.get());
    parent->addChild(n12.get());

    n11->addChild(n21.get());
    n11->addChild(n22.get());

    n12->addChild(n23.get());
    n12->addChild(n24.get());


    ASSERT_EQ(2, parent->getChildrenCount());
    ASSERT_EQ(parent.get(), n11->getParent());
    ASSERT_EQ(parent.get(), n12->getParent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11->getChildrenCount());
    ASSERT_EQ(n11.get(), n21->getParent());
    ASSERT_EQ(n11.get(), n22->getParent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12->getChildrenCount());
    ASSERT_EQ(n12.get(), n23->getParent());
    ASSERT_EQ(n12.get(), n24->getParent());

    // all nodes should have the same root
    ASSERT_EQ(*(n11->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n21->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n22->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n12->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n23->getRoot()), reinterpret_cast<void *>(parent.get()));
    ASSERT_EQ(*(n24->getRoot()), reinterpret_cast<void *>(parent.get()));

}


static void setGetResultCommon(const std::shared_ptr<gempba::TraceNode> &node) {
    std::vector<int> vec{1, 2, 3, 4, 5};
    node->setResult(std::make_any<std::vector<int>>(std::forward<std::vector<int>>(vec)));
    auto resultAny = node->getResult();
    ASSERT_TRUE(resultAny.has_value());

    auto result = std::any_cast<std::vector<int>>(resultAny);

    std::vector<int> expVec{1, 2, 3, 4, 5};
    ASSERT_EQ(expVec, result);
    ASSERT_EQ(0, vec.size());
    ASSERT_EQ(5, expVec.size());

    ASSERT_TRUE(node->isConsumed());
    ASSERT_EQ(gempba::RETRIEVED, node->getState());
}

TEST_F(TraceNodeTest, SetGetResultDummyNode) {
    auto node = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    setGetResultCommon(node);
}

TEST_F(TraceNodeTest, SetGetResultNonDummyNode) {
    auto node = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);
    setGetResultCommon(node);
}


TEST_F(TraceNodeTest, SetGetFutureResultNonDummyNode) {
    auto node = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);

    std::promise<std::vector<int>> promise;
    node->setFutureResult(utils::convert_to_any_future(promise.get_future()));

    promise.set_value(std::vector<int>{1, 2, 3, 4, 5});

    auto anyResult = node->getResult();
    ASSERT_TRUE(anyResult.has_value());

    auto &&result = std::any_cast<std::vector<int>>(anyResult);

    printf("Result in test retrieved\n");

    std::vector<int> expVec{1, 2, 3, 4, 5};

    ASSERT_EQ(expVec, result);
}

TEST_F(TraceNodeTest, SetGetStates) {
    auto node = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);

    ASSERT_EQ(gempba::UNUSED, node->getState());
    ASSERT_FALSE(node->isConsumed());

    node->setState(gempba::PUSHED);
    ASSERT_EQ(gempba::PUSHED, node->getState());
    ASSERT_TRUE(node->isConsumed());

    node->setState(gempba::FORWARDED);
    ASSERT_EQ(gempba::FORWARDED, node->getState());
    ASSERT_TRUE(node->isConsumed());

    node->setState(gempba::DISCARDED);
    ASSERT_EQ(gempba::DISCARDED, node->getState());
    ASSERT_TRUE(node->isConsumed());
}


TEST_F(TraceNodeTest, SetGetBranchEvaluator) {
    auto node = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);

    node->setBranchEvaluator([]() { return true; });
    ASSERT_TRUE(node->isBranchWorthExploring());

    node->setState(gempba::DISCARDED);
    ASSERT_FALSE(node->isBranchWorthExploring());

    node->setBranchEvaluator([]() { return false; });
    ASSERT_FALSE(node->isBranchWorthExploring());

    node->setState(gempba::UNUSED);
    ASSERT_FALSE(node->isBranchWorthExploring());
}

TEST_F(TraceNodeTest, IsResultRetrievable) {
    auto node = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);

    node->setResult(5);
    ASSERT_TRUE(node->isResultReady());
    auto actualAny = node->getResult();
    ASSERT_TRUE(actualAny.has_value());

    auto actual = std::any_cast<int>(actualAny);
    ASSERT_EQ(5, actual);

    ASSERT_FALSE(node->isResultReady());
}

#ifdef MULTIPROCESSING_ENABLED
auto deserializerMock = [](std::stringstream &ss, float &args) {
    char dummy[4];
    ss.read(dummy, sizeof(dummy));
    memccpy(&args, dummy, sizeof(dummy), sizeof(float));
};

TEST_F(TraceNodeTest, GetWithDeserializer) {
    //TODO ... mock MPI?
    /* auto &branchHandler = gempba::BranchHandler::getInstance(); // parallel library
     branchHandler.passMPIScheduler(&mpiScheduler);

     auto node = gempba::TraceNodeFactory::createNode<int>(branchHandler, balancerMock, -1, nullptr);

     float expected = 77.85;
     char expectedSerialized[4];
     memccpy(expectedSerialized, &expected, sizeof(expected), sizeof(expectedSerialized));

     int rank = mpiScheduler.rank_me();

     MPI_Send(expectedSerialized, 4, MPI_CHAR, rank, node->getNodeId(), MPI_COMM_WORLD);

     node->setResult(expected);
     node->setSentToAnotherProcess(rank);

     auto actual = node->get<float>(deserializerMock);

     ASSERT_EQ(expected, actual);*/
}

#endif


TEST_F(TraceNodeTest, ForwardPushCountAndThreadId) {

    int expectedThreadId = 17;
    auto node = gempba::TraceNodeFactory::createNode<int>(balancerMock, expectedThreadId, nullptr);

    ASSERT_EQ(0, node->getForwardCount());
    ASSERT_EQ(0, node->getPushCount());

    node->setState(gempba::FORWARDED);
    ASSERT_EQ(1, node->getForwardCount());

    node->setState(gempba::PUSHED);
    ASSERT_EQ(1, node->getPushCount());

    ASSERT_EQ(expectedThreadId, node->getThreadId());
}

TEST_F(TraceNodeTest, GetNodeId) {
    auto node1 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);
    auto node2 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, node1.get());

    ASSERT_EQ(1, node1->getNodeId());
    ASSERT_EQ(2, node2->getNodeId());
}

TEST_F(TraceNodeTest, GetArgumentsCopy) {
    auto nodeBase = gempba::TraceNodeFactory::createNode<MyStruct, float, double, std::vector<float>>(balancerMock, -1, nullptr);

    MyStruct ins{7};
    float f1 = 1.0f;
    double d1 = 3.0;
    std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    auto node = dynamic_cast< gempba::AbstractTraceNode<MyStruct, float, double, std::vector<float>> *>(nodeBase.get());

    node->setArguments(ins, f1, d1, vec);

    auto argsRef = node->getArgumentsRef();
    auto argsCopy = node->getArgumentsCopy();

    //assert that the copy is equal to the reference
    ASSERT_EQ(argsRef, argsCopy);

    //assert expected values
    auto &insCpy = std::get<0>(argsCopy);
    auto fCpy = std::get<1>(argsCopy);
    auto dCpy = std::get<2>(argsCopy);
    auto vecCpy = std::get<3>(argsCopy);

    auto expStruct = MyStruct{7};
    ASSERT_EQ(expStruct, insCpy);
    ASSERT_FLOAT_EQ(1.0f, fCpy);
    ASSERT_DOUBLE_EQ(3.0, dCpy);

    std::vector<float> expVec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    ASSERT_EQ(10, vecCpy.size());
    ASSERT_EQ(expVec, vecCpy);

    //assert that the copy is not a reference to the original
    std::get<0>(argsRef) = MyStruct{8};
    std::get<1>(argsRef) = 2.0f;
    std::get<2>(argsRef) = 4.0;
    std::get<3>(argsRef) = std::vector<float>{2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f};

    ASSERT_EQ(expStruct, insCpy);
    ASSERT_FLOAT_EQ(1.0f, fCpy);
    ASSERT_DOUBLE_EQ(3.0, dCpy);
    ASSERT_EQ(10, vecCpy.size());
    ASSERT_EQ(expVec, vecCpy);
}

TEST_F(TraceNodeTest, GetChildren) {
    auto dummy = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    auto child1 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child2 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child3 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());

    auto children = dummy->getChildren();
    ASSERT_EQ(3, children.size());
    ASSERT_EQ(child1.get(), dummy->getFirstChild());

    auto it = children.begin();
    ASSERT_EQ(child1.get(), *(it++));
    ASSERT_EQ(child2.get(), *(it++));
    ASSERT_EQ(child3.get(), *it);
}

TEST_F(TraceNodeTest, PruneFrontChild) {
    auto dummy = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    auto child1 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child2 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child3 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());

    // assert existing children
    auto children = dummy->getChildren();

    auto it = children.begin();
    ASSERT_EQ(child1.get(), *(it++));
    ASSERT_EQ(child2.get(), *(it++));
    ASSERT_EQ(child3.get(), *it);

    // prune first child
    dummy->pruneFrontChild();
    children = dummy->getChildren();
    it = children.begin();
    ASSERT_EQ(2, children.size());
    ASSERT_EQ(child2.get(), *(it++));
    ASSERT_EQ(child3.get(), *it);

    // prune first child again
    dummy->pruneFrontChild();
    children = dummy->getChildren();
    it = children.begin();
    ASSERT_EQ(child3.get(), *it);
    ASSERT_EQ(1, children.size());

    // prune first child again
    dummy->pruneFrontChild();
    children = dummy->getChildren();
    ASSERT_EQ(0, children.size());

}

TEST_F(TraceNodeTest, PruneSecondChild) {
    auto dummy = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    auto child1 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child2 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child3 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());

    // assert existing children
    auto children = dummy->getChildren();

    auto it = children.begin();
    ASSERT_EQ(child1.get(), *(it++));
    ASSERT_EQ(child2.get(), *(it++));
    ASSERT_EQ(child3.get(), *it);

    // prune second child

    dummy->pruneSecondChild();
    children = dummy->getChildren();
    it = children.begin();
    ASSERT_EQ(child1.get(), *(it++));
    ASSERT_EQ(child3.get(), *it);
    ASSERT_EQ(2, children.size());

    // prune second child again
    dummy->pruneSecondChild();
    children = dummy->getChildren();
    it = children.begin();
    ASSERT_EQ(child1.get(), *it);
    ASSERT_EQ(1, children.size());

    // prune second child again
    try {
        dummy->pruneSecondChild();
        FAIL() << "Expected exception";
    } catch (const std::exception &e) {
        ASSERT_STREQ("Cannot prune second child when there are less than 2 children", e.what());
    }
}

TEST_F(TraceNodeTest, GetSecondChild) {
    auto dummy = gempba::TraceNodeFactory::createDummy(balancerMock, -1);
    auto child1 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child2 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());
    auto child3 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, dummy.get());

    // assert existing children
    auto children = dummy->getChildren();

    auto it = children.begin();
    ASSERT_EQ(child1.get(), *(it++));
    ASSERT_EQ(child2.get(), *(it++));
    ASSERT_EQ(child3.get(), *it);

    // get second child
    ASSERT_EQ(child2.get(), dummy->getSecondChild());
}

TEST_F(TraceNodeTest, GetSiblings) {
    auto dummy = gempba::TraceNodeFactory::createDummy(balancerMock, -1);

    ASSERT_EQ(nullptr, dummy->getFirstSibling());
    ASSERT_EQ(nullptr, dummy->getPreviousSibling());
    ASSERT_EQ(nullptr, dummy->getNextSibling());

    auto parent = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, nullptr);
    ASSERT_EQ(nullptr, parent->getFirstSibling());
    ASSERT_EQ(nullptr, parent->getPreviousSibling());
    ASSERT_EQ(nullptr, parent->getNextSibling());

    auto child1 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());
    auto child2 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());
    auto child3 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());
    auto child4 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());
    auto child5 = gempba::TraceNodeFactory::createNode<int>(balancerMock, -1, parent.get());

    ASSERT_EQ(child1.get(), child1->getFirstSibling());
    ASSERT_EQ(nullptr, child1->getPreviousSibling());
    ASSERT_EQ(child2.get(), child1->getNextSibling());

    ASSERT_EQ(child1.get(), child2->getFirstSibling());
    ASSERT_EQ(child1.get(), child2->getPreviousSibling());
    ASSERT_EQ(child3.get(), child2->getNextSibling());

    ASSERT_EQ(child1.get(), child3->getFirstSibling());
    ASSERT_EQ(child2.get(), child3->getPreviousSibling());
    ASSERT_EQ(child4.get(), child3->getNextSibling());

    ASSERT_EQ(child1.get(), child4->getFirstSibling());
    ASSERT_EQ(child3.get(), child4->getPreviousSibling());
    ASSERT_EQ(child5.get(), child4->getNextSibling());

    ASSERT_EQ(child1.get(), child5->getFirstSibling());
    ASSERT_EQ(child4.get(), child5->getPreviousSibling());
    ASSERT_EQ(nullptr, child5->getNextSibling());

}

