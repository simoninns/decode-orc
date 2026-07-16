/*
 * File:        bounded_queue_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for the EFM pipeline bounded blocking queue (P-9)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "bounded_queue.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace {

// A single-threaded producer/consumer that closes after the last item drains
// in FIFO order.
TEST(BoundedQueueTest, DeliversItemsInFifoOrderThenReportsEndOfStream) {
  BoundedQueue<int> queue(8);
  for (int i = 0; i < 5; ++i) {
    int v = i;
    EXPECT_TRUE(queue.push(std::move(v)));
  }
  queue.close();

  for (int i = 0; i < 5; ++i) {
    int out = -1;
    ASSERT_TRUE(queue.pop(out));
    EXPECT_EQ(out, i);
  }

  // Drained after close(): pop() reports end-of-stream.
  int out = -1;
  EXPECT_FALSE(queue.pop(out));
}

// close() on an empty queue makes pop() return immediately.
TEST(BoundedQueueTest, CloseOnEmptyQueueEndsConsumer) {
  BoundedQueue<int> queue(4);
  queue.close();
  int out = 0;
  EXPECT_FALSE(queue.pop(out));
}

// After abort(), producers and consumers are rejected and queued items dropped.
TEST(BoundedQueueTest, AbortRejectsPushAndDiscardsQueuedItems) {
  BoundedQueue<int> queue(4);
  int a = 1;
  EXPECT_TRUE(queue.push(std::move(a)));
  queue.abort();

  int out = -1;
  EXPECT_FALSE(queue.pop(out));  // queued item discarded on abort
  int b = 2;
  EXPECT_FALSE(queue.push(std::move(b)));  // further pushes rejected
}

// A small capacity forces the producer to block on a full queue; the consumer
// draining in another thread must receive every item exactly once, in order.
// This exercises the blocking + back-pressure + hand-off paths.
TEST(BoundedQueueTest, BlockingHandOffPreservesEveryItemInOrder) {
  constexpr int kCount = 2000;
  BoundedQueue<int> queue(4);  // far smaller than kCount -> repeated blocking

  std::vector<int> received;
  received.reserve(kCount);
  std::thread consumer([&] {
    int out = -1;
    while (queue.pop(out)) received.push_back(out);
  });

  for (int i = 0; i < kCount; ++i) {
    int v = i;
    ASSERT_TRUE(queue.push(std::move(v)));
  }
  queue.close();
  consumer.join();

  ASSERT_EQ(static_cast<int>(received.size()), kCount);
  for (int i = 0; i < kCount; ++i) EXPECT_EQ(received[i], i);
}

// abort() must wake a producer blocked on a full queue so a stalled consumer
// cannot deadlock the producer (the error / cancel tear-down path).
TEST(BoundedQueueTest, AbortWakesBlockedProducer) {
  BoundedQueue<int> queue(1);
  bool sawRejection = false;
  std::thread producer([&] {
    int i = 0;
    // The queue never drains, so once it is full this push() blocks until
    // abort() is called, after which it returns false.
    while (true) {
      int v = i++;
      if (!queue.push(std::move(v))) {
        sawRejection = true;
        break;
      }
    }
  });

  queue.abort();
  producer.join();  // deadlocks the test if abort() failed to wake the producer
  EXPECT_TRUE(sawRejection);
}

}  // namespace
