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

#pragma once

#include <zc/core/common.h>
#include <zc/core/debug.h>
#include <zc/core/list.h>
#include <zc/core/memory.h>

#include <list>

#include "zc/async/async.h"

ZC_BEGIN_HEADER

namespace zc {

template <typename T>
class WaiterQueue {
public:
  // A WaiterQueue creates Nodes that blend newAdaptedPromise<T, Adaptor> and List<Node>.

  WaiterQueue() = default;
  ZC_DISALLOW_COPY_AND_MOVE(WaiterQueue);

  Promise<T> wait() { return newAdaptedPromise<T, Node>(queue); }

  void fulfill(T&& value) {
    ZC_IREQUIRE(!empty());

    auto& node = static_cast<Node&>(queue.front());
    node.fulfiller.fulfill(zc::mv(value));
    node.remove();
  }

  void reject(Exception&& exception) {
    ZC_IREQUIRE(!empty());

    auto& node = static_cast<Node&>(queue.front());
    node.fulfiller.reject(zc::mv(exception));
    node.remove();
  }

  bool empty() const { return queue.empty(); }

private:
  struct BaseNode {
    // This is a separate structure because List requires a predefined memory layout but
    // newAdaptedPromise() only provides access to the Adaptor type in the ctor.

    BaseNode(PromiseFulfiller<T>& fulfiller) : fulfiller(fulfiller) {}

    PromiseFulfiller<T>& fulfiller;
    ListLink<BaseNode> link;
  };

  using Queue = List<BaseNode, &BaseNode::link>;

  struct Node : public BaseNode {
    Node(PromiseFulfiller<T>& fulfiller, Queue& queue) : BaseNode(fulfiller), queue(queue) {
      queue.add(*this);
    }

    ~Node() noexcept(false) {
      // When the associated Promise is destructed, so is the Node thus we should leave the queue.
      remove();
    }

    void remove() {
      if (BaseNode::link.isLinked()) { queue.remove(*this); }
    }

    Queue& queue;
  };

  Queue queue;
};

template <typename T>
class ProducerConsumerQueue {
  // ProducerConsumerQueue is an async FIFO queue.

public:
  void push(T v) {
    // Push an existing value onto the queue.

    if (!waiters.empty()) {
      // We have at least one waiter, give the value to the oldest.
      ZC_IASSERT(values.empty());

      // Fulfill the first waiter and return without store our value.
      waiters.fulfill(zc::mv(v));
    } else {
      // We don't have any waiters, store the value.
      values.push_front(zc::mv(v));
    }
  }

  void rejectAll(Exception e) {
    // Reject all waiters with a given exception.

    while (!waiters.empty()) {
      auto newE = Exception(e);
      waiters.reject(zc::mv(newE));
    }
  }

  Promise<T> pop() {
    // Eventually pop a value from the queue.
    // Note that if your sinks lag your sources, the promise will always be ready.

    if (!values.empty()) {
      // We have at least one value, get the oldest.
      ZC_IASSERT(waiters.empty());

      auto value = zc::mv(values.back());
      values.pop_back();
      return zc::mv(value);
    } else {
      // We don't have any values, add ourselves to the waiting queue.
      return waiters.wait();
    }
  }

private:
  std::list<T> values;
  WaiterQueue<T> waiters;
};

}  // namespace zc

ZC_END_HEADER
