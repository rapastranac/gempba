#include <gtest/gtest.h>
#include "../../../../GemPBA/DLB/DLB_Handler.hpp"
#include "../../../../GemPBA/function_trace/factory/trace_node_factory.hpp"

#include<vector>
#include <future>

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */

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

template<typename Node>
void foo(Node node) {
    MyStruct ins{7};
    float f1 = 1.0f;
    double d1 = 3.0;
    std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    node->setArguments(ins, f1, d1, vec);
}

TEST(TraceNodeTest, SetGetArgumentsVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createVirtual<MyStruct, float, double, std::vector<float>>(handler, -1);

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

TEST(TraceNodeTest, GetPointersVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createVirtual<MyStruct, float, double, std::vector<float>>(handler, -1);

    ASSERT_TRUE(node->isVirtual());
    ASSERT_FALSE(node->getParent());
    ASSERT_TRUE(node->getRoot());
    ASSERT_EQ(*(node->getRoot()), reinterpret_cast<void *>(node->getItself()));
    ASSERT_TRUE(node->getChildren().empty());

}

TEST(TraceNodeTest, GetPointersNonVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createNode<MyStruct, float, double, std::vector<float>>(handler, -1, nullptr);

    ASSERT_FALSE(node->isVirtual());
    ASSERT_FALSE(node->getParent());
    ASSERT_TRUE(node->getRoot());
    ASSERT_EQ(*(node->getRoot()), reinterpret_cast<void *>(node->getItself()));
    ASSERT_TRUE(node->getChildren().empty());
}

TEST(TraceNodeTest, ThreeLevelNodesWithVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto parent = gempba::TraceNodeFactory::createVirtual<int>(handler, -1);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */

    auto n11 = gempba::TraceNodeFactory::createNode<int>(handler, -1, parent.get());
    auto n21 = gempba::TraceNodeFactory::createNode<int>(handler, -1, n11.get());
    auto n22 = gempba::TraceNodeFactory::createNode<int>(handler, -1, n11.get());

    auto n12 = gempba::TraceNodeFactory::createNode<int>(handler, -1, parent.get());
    auto n23 = gempba::TraceNodeFactory::createNode<int>(handler, -1, n12.get());
    auto n24 = gempba::TraceNodeFactory::createNode<int>(handler, -1, n12.get());

    ASSERT_EQ(2, parent->getChildren().size());
    ASSERT_EQ(parent.get(), n11->getParent());
    ASSERT_EQ(parent.get(), n12->getParent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11->getChildren().size());
    ASSERT_EQ(n11.get(), n21->getParent());
    ASSERT_EQ(n11.get(), n22->getParent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12->getChildren().size());
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

TEST(TraceNodeTest, SetRootAndParents) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto parent = gempba::TraceNodeFactory::createVirtual<int>(handler, -1);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */

    ASSERT_THROW(parent->setParent(parent.get()), std::runtime_error);

    // The following lines should not be instantiated in practice, yet this is only for testing purposes
    auto n11 = gempba::TraceNodeFactory::createNode<int>(handler, 1, nullptr);
    auto n21 = gempba::TraceNodeFactory::createNode<int>(handler, 2, nullptr);
    auto n22 = gempba::TraceNodeFactory::createNode<int>(handler, 3, nullptr);

    auto n12 = gempba::TraceNodeFactory::createNode<int>(handler, 4, nullptr);
    auto n23 = gempba::TraceNodeFactory::createNode<int>(handler, 5, nullptr);
    auto n24 = gempba::TraceNodeFactory::createNode<int>(handler, 6, nullptr);

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

    ASSERT_EQ(2, parent->getChildren().size());
    ASSERT_EQ(parent.get(), n11->getParent());
    ASSERT_EQ(parent.get(), n12->getParent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11->getChildren().size());
    ASSERT_EQ(n11.get(), n21->getParent());
    ASSERT_EQ(n11.get(), n22->getParent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12->getChildren().size());
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


static void setGetResultCommon(const std::shared_ptr<gempba::TraceNode<int>> &node) {
    std::vector<int> vec{1, 2, 3, 4, 5};
    node->setResult(std::move(vec));
    auto result = node->get<std::vector<int>>();

    std::vector<int> expVec{1, 2, 3, 4, 5};
    ASSERT_EQ(expVec, result);
    ASSERT_EQ(0, vec.size());
    ASSERT_EQ(5, expVec.size());

    ASSERT_TRUE(node->isConsumed());
    ASSERT_EQ(gempba::RETRIEVED, node->getState());
}

TEST(TraceNodeTest, SetGetResultVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createVirtual<int>(handler, -1);

    setGetResultCommon(node);
}

TEST(TraceNodeTest, SetGetResultNonVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createNode<int>(handler, -1, nullptr);

    setGetResultCommon(node);
}


TEST(TraceNodeTest, SetGetFutureResultNonVirtualNode) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createNode<int>(handler, -1, nullptr);

    std::promise<std::vector<int>> promise;
    node->setFutureResult(promise.get_future());

    promise.set_value(std::vector<int>{1, 2, 3, 4, 5});

    auto &&result = node->get<std::vector<int>>();

    printf("Result in test retrieved\n");

    std::vector<int> expVec{1, 2, 3, 4, 5};

    ASSERT_EQ(expVec, result);
}

TEST(TraceNodeTest, SetGetStates) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createNode<int>(handler, -1, nullptr);

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


TEST(TraceNodeTest, SetGetBranchEvaluator) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createVirtual<int>(handler, -1);

    node->setBranchEvaluator([]() { return true; });
    ASSERT_TRUE(node->isBranchWorthExploring());

    node->setState(gempba::DISCARDED);
    ASSERT_FALSE(node->isBranchWorthExploring());

    node->setBranchEvaluator([]() { return false; });
    ASSERT_FALSE(node->isBranchWorthExploring());

    node->setState(gempba::UNUSED);
    ASSERT_FALSE(node->isBranchWorthExploring());
}

TEST(TraceNodeTest, IsResultRetrievable) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createNode<int>(handler, -1, nullptr);

    node->setResult(5);
    ASSERT_TRUE(node->isResultRetrievable());

    auto actual = node->get<int>();
    ASSERT_EQ(5, actual);

    ASSERT_FALSE(node->isResultRetrievable());
}

#ifdef MULTIPROCESSING_ENABLED
auto deserializerMock = [](std::stringstream &ss, float &args) {
    char dummy[4];
    ss.read(dummy, sizeof(dummy));
    memccpy(&args, dummy, sizeof(dummy), sizeof(float));
};

/*TEST(TraceNodeTest, GetWithDeserializer) {
    auto &dlb = gempba::DLB_Handler::getInstance();
    auto &mpiScheduler = gempba::MPI_Scheduler::getInstance();
    auto &branchHandler = gempba::BranchHandler::getInstance(); // parallel library
    branchHandler.passMPIScheduler(&mpiScheduler);

    auto node = gempba::TraceNodeFactory::createNode<int>(branchHandler, dlb, -1, nullptr);

    float expected = 77.85;
    char expectedSerialized[4];
    memccpy(expectedSerialized, &expected, sizeof(expected), sizeof(expectedSerialized));

    int rank = mpiScheduler.rank_me();

    MPI_Send(expectedSerialized, 4, MPI_CHAR, rank, node->getNodeId(), MPI_COMM_WORLD);

    node->setResult(expected);
    node->setSentToAnotherProcess(rank);

    auto actual = node->get<float>(deserializerMock);

    ASSERT_EQ(expected, actual);
}*/

#endif


TEST(TraceNodeTest, ForwardPushCountAndThreadId) {
    auto &handler = gempba::DLB_Handler::getInstance();

    int expectedThreadId = 17;
    auto node = gempba::TraceNodeFactory::createNode<int>(handler, expectedThreadId, nullptr);

    ASSERT_EQ(0, node->getForwardCount());
    ASSERT_EQ(0, node->getPushCount());

    node->setState(gempba::FORWARDED);
    ASSERT_EQ(1, node->getForwardCount());

    node->setState(gempba::PUSHED);
    ASSERT_EQ(1, node->getPushCount());

    ASSERT_EQ(expectedThreadId, node->getThreadId());
}

TEST(TraceNodeTest, GetArgumentsCopy) {
    auto &handler = gempba::DLB_Handler::getInstance();
    auto node = gempba::TraceNodeFactory::createNode<MyStruct, float, double, std::vector<float>>(handler, -1, nullptr);

    MyStruct ins{7};
    float f1 = 1.0f;
    double d1 = 3.0;
    std::vector<float> vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

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

