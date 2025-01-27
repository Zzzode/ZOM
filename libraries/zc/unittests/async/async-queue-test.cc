// Copyright (c) 2021 Cloudflare, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "zc/async/async-queue.h"

#include <zc/async/async-io.h>
#include <zc/core/vector.h>
#include <zc/ztest/test.h>

namespace zc {
namespace {

struct QueueTest {
  zc::AsyncIoContext io = setupAsyncIo();
  ProducerConsumerQueue<size_t> queue;

  QueueTest() = default;
  QueueTest(QueueTest&&) = delete;
  QueueTest(const QueueTest&) = delete;
  QueueTest& operator=(QueueTest&&) = delete;
  QueueTest& operator=(const QueueTest&) = delete;

  struct Producer {
    QueueTest& test;
    Promise<void> promise = zc::READY_NOW;

    Producer(QueueTest& test) : test(test) {}

    void push(size_t i) {
      auto push = [&, i]() -> Promise<void> {
        test.queue.push(i);
        return zc::READY_NOW;
      };
      promise = promise.then(zc::mv(push));
    }
  };

  struct Consumer {
    QueueTest& test;
    Promise<void> promise = zc::READY_NOW;

    Consumer(QueueTest& test) : test(test) {}

    void pop(Vector<bool>& bits) {
      auto pop = [&]() { return test.queue.pop(); };
      auto checkPop = [&](size_t j) -> Promise<void> {
        bits[j] = true;
        return zc::READY_NOW;
      };
      promise = promise.then(zc::mv(pop)).then(zc::mv(checkPop));
    }
  };
};

ZC_TEST("ProducerConsumerQueue with various amounts of producers and consumers") {
  QueueTest test;

  size_t constexpr kItemCount = 1000;
  for (auto producerCount : {1, 5, 10}) {
    for (auto consumerCount : {1, 5, 10}) {
      ZC_LOG(INFO, "Testing a new set of Producers and Consumers",  //
             producerCount, consumerCount, kItemCount);
      // Make a vector to track our entries.
      auto bits = Vector<bool>(kItemCount);
      for (auto i ZC_UNUSED : zc::zeroTo(kItemCount)) { bits.add(false); }

      // Make enough producers.
      auto producers = Vector<QueueTest::Producer>();
      for (auto i ZC_UNUSED : zc::zeroTo(producerCount)) { producers.add(test); }

      // Make enough consumers.
      auto consumers = Vector<QueueTest::Consumer>();
      for (auto i ZC_UNUSED : zc::zeroTo(consumerCount)) { consumers.add(test); }

      for (auto i : zc::zeroTo(kItemCount)) {
        // Use a producer and a consumer for each entry.

        auto& producer = producers[i % producerCount];
        producer.push(i);

        auto& consumer = consumers[i % consumerCount];
        consumer.pop(bits);
      }

      // Confirm that all entries are produced and consumed.
      auto promises = Vector<Promise<void>>();
      for (auto& producer : producers) { promises.add(zc::mv(producer.promise)); }
      for (auto& consumer : consumers) { promises.add(zc::mv(consumer.promise)); }
      joinPromises(promises.releaseAsArray()).wait(test.io.waitScope);
      for (auto i : zc::zeroTo(kItemCount)) { ZC_ASSERT(bits[i], i); }
    }
  }
}

ZC_TEST("ProducerConsumerQueue with rejectAll()") {
  QueueTest test;

  for (auto consumerCount : {1, 5, 10}) {
    ZC_LOG(INFO, "Testing a new set of consumers with rejection", consumerCount);

    // Make enough consumers.
    auto promises = Vector<Promise<void>>();
    for (auto i ZC_UNUSED : zc::zeroTo(consumerCount)) {
      promises.add(test.queue.pop().ignoreResult());
    }

    for (auto& promise : promises) {
      ZC_EXPECT(!promise.poll(test.io.waitScope), "All of our consumers should be waiting");
    }
    test.queue.rejectAll(ZC_EXCEPTION(FAILED, "Total rejection"));

    // We should have finished and swallowed the errors.
    auto promise = joinPromises(promises.releaseAsArray());
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("Total rejection", promise.wait(test.io.waitScope));
  }
}

}  // namespace
}  // namespace zc
