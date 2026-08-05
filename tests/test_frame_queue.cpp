#include <gtest/gtest.h>
#include "core/frame_queue.h"
#include <thread>
#include <chrono>

using namespace xrk;

class FrameQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(FrameQueueTest, FifoOrder) {
    FrameQueue<int> queue(3);
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);

    EXPECT_EQ(queue.size(), 3);
    EXPECT_EQ(queue.capacity(), 3);
    EXPECT_FALSE(queue.isEmpty());

    EXPECT_EQ(queue.dequeue(10), 1);
    EXPECT_EQ(queue.dequeue(10), 2);
    EXPECT_EQ(queue.dequeue(10), 3);
    EXPECT_TRUE(queue.isEmpty());
}

TEST_F(FrameQueueTest, EnqueueNonBlockingFull) {
    FrameQueue<QString> queue(2);
    EXPECT_TRUE(queue.enqueueNonBlocking("a"));
    EXPECT_TRUE(queue.enqueueNonBlocking("b"));
    EXPECT_FALSE(queue.enqueueNonBlocking("c"));
    EXPECT_EQ(queue.size(), 2);

    EXPECT_TRUE(queue.dequeue(10) == "a");
    EXPECT_TRUE(queue.enqueueNonBlocking("c"));
    EXPECT_EQ(queue.size(), 2);
}

TEST_F(FrameQueueTest, DequeueTimeoutOnEmpty) {
    FrameQueue<int> queue(1);
    auto start = std::chrono::steady_clock::now();
    int value = queue.dequeue(30);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(value, 0);
    EXPECT_GE(elapsed, 25);
}

TEST_F(FrameQueueTest, Clear) {
    FrameQueue<int> queue(3);
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);
    queue.clear();
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_EQ(queue.size(), 0);

    queue.enqueue(42);
    EXPECT_EQ(queue.dequeue(10), 42);
}

TEST_F(FrameQueueTest, BlockingEnqueueWhenFull) {
    FrameQueue<int> queue(2);
    queue.enqueue(1);
    queue.enqueue(2);

    std::thread consumer([&queue]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        EXPECT_EQ(queue.dequeue(2000), 1);
    });

    queue.enqueue(3);

    consumer.join();
    EXPECT_EQ(queue.size(), 2);
    EXPECT_EQ(queue.dequeue(10), 2);
    EXPECT_EQ(queue.dequeue(10), 3);
}

TEST_F(FrameQueueTest, DefaultCapacity) {
    FrameQueue<QByteArray> queue;
    EXPECT_EQ(queue.capacity(), 3);
    EXPECT_TRUE(queue.isEmpty());
}
