// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "zc/async/async.h"

#include <zc/ztest/gtest.h>

#include "zc/core/array.h"
#include "zc/core/debug.h"
#include "zc/core/mutex.h"
#include "zc/core/thread.h"
#include "zc/ztest/test.h"

#if !_WIN32
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#endif

#if ZC_USE_FIBERS && __linux__
#include <errno.h>
#include <ucontext.h>
#endif

namespace zc {
namespace {

#if !_MSC_VER
// TODO(msvc): GetFunctorStartAddress is not supported on MSVC currently, so skip the test.
TEST(Async, GetFunctorStartAddress) {
  EXPECT_TRUE(nullptr != _::GetFunctorStartAddress<>::apply([]() { return 0; }));
}
#endif

#if ZC_USE_FIBERS
bool isLibcContextHandlingKnownBroken() {
  // manylinux2014-x86's libc implements getcontext() to fail with ENOSYS. This is flagrantly
  // against spec: getcontext() is not a syscall and is documented as never failing. Our configure
  // script cannot detect this problem because it would require actually executing code to see
  // what happens, which wouldn't work when cross-compiling. It would have been so much better if
  // they had removed the symbol from libc entirely. But as a work-around, we will skip the tests
  // when libc is broken.
#if __linux__
  static bool result = ([]() {
    ucontext_t context;
    if (getcontext(&context) < 0 && errno == ENOSYS) {
      ZC_LOG(WARNING,
             "This platform's libc is broken. Its getcontext() errors with ENOSYS. Fibers will not "
             "work, so we'll skip the tests, but libzc was still built with fiber support, which "
             "is broken. Please tell your libc maitnainer to remove the getcontext() function "
             "entirely rather than provide an intentionally-broken version -- that way, the "
             "configure script will detect that it should build libzc without fiber support.");
      return true;
    } else {
      return false;
    }
  })();
  return result;
#else
  return false;
#endif
}
#endif

TEST(Async, EvalVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<void> promise = evalLater([&]() { done = true; });
  EXPECT_FALSE(done);
  promise.wait(waitScope);
  EXPECT_TRUE(done);
}

TEST(Async, EvalInt) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<int> promise = evalLater([&]() {
    done = true;
    return 123;
  });
  EXPECT_FALSE(done);
  EXPECT_EQ(123, promise.wait(waitScope));
  EXPECT_TRUE(done);
}

TEST(Async, There) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> a = 123;
  bool done = false;

  Promise<int> promise = a.then([&](int ai) {
    done = true;
    return ai + 321;
  });
  EXPECT_FALSE(done);
  EXPECT_EQ(444, promise.wait(waitScope));
  EXPECT_TRUE(done);
}

TEST(Async, ThereVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> a = 123;
  int value = 0;

  Promise<void> promise = a.then([&](int ai) { value = ai; });
  EXPECT_EQ(0, value);
  promise.wait(waitScope);
  EXPECT_EQ(123, value);
}

TEST(Async, Exception) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() -> int {
    ZC_FAIL_ASSERT("foo") { return 123; }
  });
  ZC_EXPECT_THROW_MESSAGE("foo", promise.wait(waitScope));
}

TEST(Async, HandleException) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() -> int {
    ZC_FAIL_ASSERT("foo") { return 123; }
  });
  int line = __LINE__ - 1;

  promise = promise.then([](int i) { return i + 1; },
                         [&](Exception&& e) {
                           EXPECT_EQ(line, e.getLine() + 1);
                           return 345;
                         });

  EXPECT_EQ(345, promise.wait(waitScope));
}

TEST(Async, PropagateException) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() -> int {
    ZC_FAIL_ASSERT("foo") { return 123; }
  });
  int line = __LINE__ - 1;

  promise = promise.then([](int i) { return i + 1; });

  promise = promise.then([](int i) { return i + 2; },
                         [&](Exception&& e) {
                           EXPECT_EQ(line, e.getLine() + 1);
                           return 345;
                         });

  EXPECT_EQ(345, promise.wait(waitScope));
}

TEST(Async, PropagateExceptionTypeChange) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() -> int {
    ZC_FAIL_ASSERT("foo") { return 123; }
  });
  int line = __LINE__ - 1;

  Promise<StringPtr> promise2 = promise.then([](int i) -> StringPtr { return "foo"; });

  promise2 = promise2.then([](StringPtr s) -> StringPtr { return "bar"; },
                           [&](Exception&& e) -> StringPtr {
                             EXPECT_EQ(line, e.getLine() + 1);
                             return "baz";
                           });

  EXPECT_EQ("baz", promise2.wait(waitScope));
}

TEST(Async, Then) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<int> promise = Promise<int>(123).then([&](int i) {
    done = true;
    return i + 321;
  });

  EXPECT_FALSE(done);

  EXPECT_EQ(444, promise.wait(waitScope));

  EXPECT_TRUE(done);
}

TEST(Async, Chain) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() -> int { return 123; });
  Promise<int> promise2 = evalLater([&]() -> int { return 321; });

  auto promise3 = promise.then([&](int i) { return promise2.then([i](int j) { return i + j; }); });

  EXPECT_EQ(444, promise3.wait(waitScope));
}

TEST(Async, DeepChain) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = NEVER_DONE;

  // Create a ridiculous chain of promises.
  for (uint i = 0; i < 1000; i++) {
    promise = evalLater([promise = zc::mv(promise)]() mutable { return zc::mv(promise); });
  }

  loop.run();

  auto trace = promise.trace();
  uint lines = 0;
  for (char c : trace) { lines += c == '\n'; }

  // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
  // 2-ish nodes.  We'll give a little room for implementation freedom.
  EXPECT_LT(lines, 5);
}

TEST(Async, DeepChain2) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = nullptr;
  promise = evalLater([&]() {
    auto trace = promise.trace();
    uint lines = 0;
    for (char c : trace) { lines += c == '\n'; }

    // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
    // 2-ish nodes.  We'll give a little room for implementation freedom.
    EXPECT_LT(lines, 5);
  });

  // Create a ridiculous chain of promises.
  for (uint i = 0; i < 1000; i++) {
    promise = evalLater([promise = zc::mv(promise)]() mutable { return zc::mv(promise); });
  }

  promise.wait(waitScope);
}

Promise<void> makeChain(uint i) {
  if (i > 0) {
    return evalLater([i]() -> Promise<void> { return makeChain(i - 1); });
  } else {
    return NEVER_DONE;
  }
}

TEST(Async, DeepChain3) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = makeChain(1000);

  loop.run();

  auto trace = promise.trace();
  uint lines = 0;
  for (char c : trace) { lines += c == '\n'; }

  // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
  // 2-ish nodes.  We'll give a little room for implementation freedom.
  EXPECT_LT(lines, 5);
}

Promise<void> makeChain2(uint i, Promise<void> promise) {
  if (i > 0) {
    return evalLater([i, promise = zc::mv(promise)]() mutable -> Promise<void> {
      return makeChain2(i - 1, zc::mv(promise));
    });
  } else {
    return zc::mv(promise);
  }
}

TEST(Async, DeepChain4) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = nullptr;
  promise = evalLater([&]() {
    auto trace = promise.trace();
    uint lines = 0;
    for (char c : trace) { lines += c == '\n'; }

    // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
    // 2-ish nodes.  We'll give a little room for implementation freedom.
    EXPECT_LT(lines, 5);
  });

  promise = makeChain2(1000, zc::mv(promise));

  promise.wait(waitScope);
}

TEST(Async, IgnoreResult) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<void> promise = Promise<int>(123)
                              .then([&](int i) {
                                done = true;
                                return i + 321;
                              })
                              .ignoreResult();

  EXPECT_FALSE(done);

  promise.wait(waitScope);

  EXPECT_TRUE(done);
}

TEST(Async, SeparateFulfiller) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(123);
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  EXPECT_EQ(123, pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill();
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  pair.promise.wait(waitScope);
}

TEST(Async, SeparateFulfillerCanceled) {
  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.promise = nullptr;
  EXPECT_FALSE(pair.fulfiller->isWaiting());
}

TEST(Async, SeparateFulfillerChained) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<Promise<int>>();
  auto inner = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(zc::mv(inner.promise));
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  inner.fulfiller->fulfill(123);

  EXPECT_EQ(123, pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerDiscarded) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<void>();
  pair.fulfiller = nullptr;

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "PromiseFulfiller was destroyed without fulfilling the promise",
      pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerDiscardedDuringUnwind) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<int>();
  zc::runCatchingExceptions([&]() {
    auto fulfillerToDrop = zc::mv(pair.fulfiller);
    zc::throwFatalException(ZC_EXCEPTION(FAILED, "test exception"));
  });

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("test exception", pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerMemoryLeak) {
  auto paf = zc::newPromiseAndFulfiller<void>();
  paf.fulfiller->fulfill();
}

TEST(Async, Ordering) {
  EventLoop loop;
  WaitScope waitScope(loop);

  class ErrorHandlerImpl : public TaskSet::ErrorHandler {
  public:
    void taskFailed(zc::Exception&& exception) override { ZC_FAIL_EXPECT(exception); }
  };

  int counter = 0;
  ErrorHandlerImpl errorHandler;
  zc::TaskSet tasks(errorHandler);

  tasks.add(evalLater([&]() {
    EXPECT_EQ(0, counter++);

    {
      // Use a promise and fulfiller so that we can fulfill the promise after waiting on it in
      // order to induce depth-first scheduling.
      auto paf = zc::newPromiseAndFulfiller<void>();
      tasks.add(paf.promise.then([&]() { EXPECT_EQ(1, counter++); }));
      paf.fulfiller->fulfill();
    }

    // .then() is scheduled breadth-first if the promise has already resolved, but depth-first
    // if the promise resolves later.
    tasks.add(Promise<void>(READY_NOW).then([&]() { EXPECT_EQ(4, counter++); }).then([&]() {
      EXPECT_EQ(5, counter++);
      tasks.add(zc::evalLast([&]() {
        EXPECT_EQ(7, counter++);
        tasks.add(zc::evalLater([&]() { EXPECT_EQ(8, counter++); }));
      }));
    }));

    {
      auto paf = zc::newPromiseAndFulfiller<void>();
      tasks.add(paf.promise.then([&]() {
        EXPECT_EQ(2, counter++);
        tasks.add(zc::evalLast([&]() {
          EXPECT_EQ(9, counter++);
          tasks.add(zc::evalLater([&]() { EXPECT_EQ(10, counter++); }));
        }));
      }));
      paf.fulfiller->fulfill();
    }

    // evalLater() is like READY_NOW.then().
    tasks.add(evalLater([&]() { EXPECT_EQ(6, counter++); }));
  }));

  tasks.add(evalLater([&]() {
    EXPECT_EQ(3, counter++);

    // Making this a chain should NOT cause it to preempt the first promise.  (This was a problem
    // at one point.)
    return Promise<void>(READY_NOW);
  }));

  tasks.onEmpty().wait(waitScope);

  EXPECT_EQ(11, counter);
}

TEST(Async, Fork) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() { return 123; });

  auto fork = promise.fork();

#if __GNUC__ && !__clang__ && __GNUC__ >= 7
// GCC 7 decides the open-brace below is "misleadingly indented" as if it were guarded by the `for`
// that appears in the implementation of ZC_REQUIRE(). Shut up shut up shut up.
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
  ZC_ASSERT(!fork.hasBranches());
  {
    auto cancelBranch = fork.addBranch();
    ZC_ASSERT(fork.hasBranches());
  }
  ZC_ASSERT(!fork.hasBranches());

  auto branch1 = fork.addBranch().then([](int i) {
    EXPECT_EQ(123, i);
    return 456;
  });
  ZC_ASSERT(fork.hasBranches());
  auto branch2 = fork.addBranch().then([](int i) {
    EXPECT_EQ(123, i);
    return 789;
  });
  ZC_ASSERT(fork.hasBranches());

  { auto releaseFork = zc::mv(fork); }

  EXPECT_EQ(456, branch1.wait(waitScope));
  EXPECT_EQ(789, branch2.wait(waitScope));
}

struct RefcountedInt : public Refcounted {
  RefcountedInt(int i) : i(i) {}
  int i;
  Own<RefcountedInt> addRef() { return zc::addRef(*this); }
};

TEST(Async, ForkRef) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<Own<RefcountedInt>> promise = evalLater([&]() { return refcounted<RefcountedInt>(123); });

  auto fork = promise.fork();

  auto branch1 = fork.addBranch().then([](Own<RefcountedInt>&& i) {
    EXPECT_EQ(123, i->i);
    return 456;
  });
  auto branch2 = fork.addBranch().then([](Own<RefcountedInt>&& i) {
    EXPECT_EQ(123, i->i);
    return 789;
  });

  { auto releaseFork = zc::mv(fork); }

  EXPECT_EQ(456, branch1.wait(waitScope));
  EXPECT_EQ(789, branch2.wait(waitScope));
}

TEST(Async, ForkMaybeRef) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<Maybe<Own<RefcountedInt>>> promise =
      evalLater([&]() { return Maybe<Own<RefcountedInt>>(refcounted<RefcountedInt>(123)); });

  auto fork = promise.fork();

  auto branch1 = fork.addBranch().then([](Maybe<Own<RefcountedInt>>&& i) {
    EXPECT_EQ(123, ZC_REQUIRE_NONNULL(i)->i);
    return 456;
  });
  auto branch2 = fork.addBranch().then([](Maybe<Own<RefcountedInt>>&& i) {
    EXPECT_EQ(123, ZC_REQUIRE_NONNULL(i)->i);
    return 789;
  });

  { auto releaseFork = zc::mv(fork); }

  EXPECT_EQ(456, branch1.wait(waitScope));
  EXPECT_EQ(789, branch2.wait(waitScope));
}

ZC_TEST("addBranchForCoAwait") {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() { return 123; });

  auto coro = [&]() -> zc::Promise<int> {
    auto fork = promise.fork();
    // do something with the branch
    co_await fork.addBranch();
    co_return co_await fork;
  };

  ZC_EXPECT(coro().wait(waitScope) == 123);
}

TEST(Async, Split) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<Tuple<int, String, Promise<int>>> promise =
      evalLater([&]() { return zc::tuple(123, str("foo"), Promise<int>(321)); });

  Tuple<Promise<int>, Promise<String>, Promise<int>> split = promise.split();

  EXPECT_EQ(123, get<0>(split).wait(waitScope));
  EXPECT_EQ("foo", get<1>(split).wait(waitScope));
  EXPECT_EQ(321, get<2>(split).wait(waitScope));
}

TEST(Async, ExclusiveJoin) {
  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = newPromiseAndFulfiller<int>();  // never fulfilled

    EXPECT_EQ(123, left.exclusiveJoin(zc::mv(right.promise)).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = newPromiseAndFulfiller<int>();  // never fulfilled
    auto right = evalLater([&]() { return 123; });

    EXPECT_EQ(123, left.promise.exclusiveJoin(zc::mv(right)).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; });

    EXPECT_EQ(123, left.exclusiveJoin(zc::mv(right)).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; }).eagerlyEvaluate(nullptr);

    EXPECT_EQ(456, left.exclusiveJoin(zc::mv(right)).wait(waitScope));
  }
}

TEST(Async, ArrayJoin) {
  for (auto specificJoinPromisesOverload :
       {+[](zc::Array<zc::Promise<int>> promises) { return joinPromises(zc::mv(promises)); },
        +[](zc::Array<zc::Promise<int>> promises) {
          return joinPromisesFailFast(zc::mv(promises));
        }}) {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto builder = heapArrayBuilder<Promise<int>>(3);
    builder.add(123);
    builder.add(456);
    builder.add(789);

    Promise<Array<int>> promise = specificJoinPromisesOverload(builder.finish());

    auto result = promise.wait(waitScope);

    ASSERT_EQ(3u, result.size());
    EXPECT_EQ(123, result[0]);
    EXPECT_EQ(456, result[1]);
    EXPECT_EQ(789, result[2]);
  }
}

TEST(Async, ArrayJoinVoid) {
  for (auto specificJoinPromisesOverload :
       {+[](zc::Array<zc::Promise<void>> promises) { return joinPromises(zc::mv(promises)); },
        +[](zc::Array<zc::Promise<void>> promises) {
          return joinPromisesFailFast(zc::mv(promises));
        }}) {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto builder = heapArrayBuilder<Promise<void>>(3);
    builder.add(READY_NOW);
    builder.add(READY_NOW);
    builder.add(READY_NOW);

    Promise<void> promise = specificJoinPromisesOverload(builder.finish());

    promise.wait(waitScope);
  }
}

TEST(Async, RaceSuccessful) {
  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = newPromiseAndFulfiller<int>();  // never fulfilled

    EXPECT_EQ(123, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right.promise))).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = newPromiseAndFulfiller<int>();  // never fulfilled
    auto right = evalLater([&]() { return 123; });

    EXPECT_EQ(123, raceSuccessful(zc::arr(zc::mv(left.promise), zc::mv(right))).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; });

    EXPECT_EQ(123, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right))).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; }).eagerlyEvaluate(nullptr);

    EXPECT_EQ(456, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right))).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() -> Promise<int> {
      zc::throwFatalException(ZC_EXCEPTION(FAILED, "evaluation failed"));
    });

    EXPECT_EQ(123, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right))).wait(waitScope));
  }
  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() -> Promise<int> {
      zc::throwFatalException(ZC_EXCEPTION(FAILED, "evaluation failed"));
    });
    auto right = evalLater([&]() { return 123; });

    EXPECT_EQ(123, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right))).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() -> Promise<int> {
      zc::throwFatalException(ZC_EXCEPTION(FAILED, "evaluation failed"));
    });
    auto right = evalLater([&]() -> Promise<int> {
      zc::throwFatalException(ZC_EXCEPTION(FAILED, "evaluation failed"));
    });

    ZC_EXPECT_THROW(FAILED, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right))).wait(waitScope));
  }

  {
    struct NoCopy {
      int i;

      NoCopy(int i) : i(i) {}
      NoCopy(const NoCopy&) = delete;

      NoCopy(NoCopy&&) = default;
      NoCopy& operator=(NoCopy&&) = default;
    };

    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([]() -> zc::Promise<NoCopy> { return NoCopy(123); });
    zc::PromiseFulfillerPair<NoCopy> right = newPromiseAndFulfiller<NoCopy>();  // never fulfilled

    EXPECT_EQ(123, raceSuccessful(zc::arr(zc::mv(left), zc::mv(right.promise))).wait(waitScope).i);
  }
}

struct Pafs {
  zc::Array<Promise<void>> promises;
  zc::Array<Own<PromiseFulfiller<void>>> fulfillers;
};

Pafs makeCompletionCountingPafs(uint count, uint& tasksCompleted) {
  auto promisesBuilder = heapArrayBuilder<Promise<void>>(count);
  auto fulfillersBuilder = heapArrayBuilder<Own<PromiseFulfiller<void>>>(count);

  for (auto ZC_UNUSED value : zeroTo(count)) {
    auto paf = newPromiseAndFulfiller<void>();
    promisesBuilder.add(paf.promise.then([&tasksCompleted]() { ++tasksCompleted; }));
    fulfillersBuilder.add(zc::mv(paf.fulfiller));
  }

  return {promisesBuilder.finish(), fulfillersBuilder.finish()};
}

TEST(Async, ArrayJoinException) {
  EventLoop loop;
  WaitScope waitScope(loop);

  uint tasksCompleted = 0;
  auto pafs = makeCompletionCountingPafs(5, tasksCompleted);
  auto& fulfillers = pafs.fulfillers;
  Promise<void> promise = joinPromises(zc::mv(pafs.promises));

  {
    uint i = 0;
    ZC_EXPECT(tasksCompleted == 0);

    // Joined tasks are not completed early.
    fulfillers[i++]->fulfill();
    ZC_EXPECT(!promise.poll(waitScope));
    ZC_EXPECT(tasksCompleted == 0);

    fulfillers[i++]->fulfill();
    ZC_EXPECT(!promise.poll(waitScope));
    ZC_EXPECT(tasksCompleted == 0);

    // Rejected tasks do not fail-fast.
    fulfillers[i++]->reject(ZC_EXCEPTION(FAILED, "Test exception"));
    ZC_EXPECT(!promise.poll(waitScope));
    ZC_EXPECT(tasksCompleted == 0);

    fulfillers[i++]->fulfill();
    ZC_EXPECT(!promise.poll(waitScope));
    ZC_EXPECT(tasksCompleted == 0);

    // The final fulfillment makes the promise ready.
    fulfillers[i++]->fulfill();
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("Test exception", promise.wait(waitScope));
    ZC_EXPECT(tasksCompleted == 4);
  }
}

TEST(Async, ArrayJoinFailFastException) {
  EventLoop loop;
  WaitScope waitScope(loop);

  uint tasksCompleted = 0;
  auto pafs = makeCompletionCountingPafs(5, tasksCompleted);
  auto& fulfillers = pafs.fulfillers;
  Promise<void> promise = joinPromisesFailFast(zc::mv(pafs.promises));

  {
    uint i = 0;
    ZC_EXPECT(tasksCompleted == 0);

    // Joined tasks are completed eagerly, not waiting until the join node is awaited.
    fulfillers[i++]->fulfill();
    ZC_EXPECT(!promise.poll(waitScope));
    ZC_EXPECT(tasksCompleted == i);

    fulfillers[i++]->fulfill();
    ZC_EXPECT(!promise.poll(waitScope));
    ZC_EXPECT(tasksCompleted == i);

    fulfillers[i++]->reject(ZC_EXCEPTION(FAILED, "Test exception"));

    // The first rejection makes the promise ready.
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("Test exception", promise.wait(waitScope));
    ZC_EXPECT(tasksCompleted == i - 1);
  }
}

TEST(Async, Canceler) {
  EventLoop loop;
  WaitScope waitScope(loop);
  Canceler canceler;

  auto never = canceler.wrap(zc::Promise<void>(zc::NEVER_DONE));
  auto now = canceler.wrap(zc::Promise<void>(zc::READY_NOW));
  auto neverI = canceler.wrap(zc::Promise<void>(zc::NEVER_DONE).then([]() { return 123u; }));
  auto nowI = canceler.wrap(zc::Promise<uint>(123u));

  ZC_EXPECT(!never.poll(waitScope));
  ZC_EXPECT(now.poll(waitScope));
  ZC_EXPECT(!neverI.poll(waitScope));
  ZC_EXPECT(nowI.poll(waitScope));

  canceler.cancel("foobar");

  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("foobar", never.wait(waitScope));
  now.wait(waitScope);
  ZC_EXPECT_THROW_MESSAGE("foobar", neverI.wait(waitScope));
  ZC_EXPECT(nowI.wait(waitScope) == 123u);
}

TEST(Async, CancelerDoubleWrap) {
  EventLoop loop;
  WaitScope waitScope(loop);

  // This used to crash.
  Canceler canceler;
  auto promise = canceler.wrap(canceler.wrap(zc::Promise<void>(zc::NEVER_DONE)));
  canceler.cancel("whoops");
}

class ErrorHandlerImpl : public TaskSet::ErrorHandler {
public:
  uint exceptionCount = 0;
  void taskFailed(zc::Exception&& exception) override {
    EXPECT_TRUE(exception.getDescription().endsWith("example TaskSet failure"));
    ++exceptionCount;
  }
};

TEST(Async, TaskSet) {
  EventLoop loop;
  WaitScope waitScope(loop);
  ErrorHandlerImpl errorHandler;
  TaskSet tasks(errorHandler);

  int counter = 0;

  tasks.add(evalLater([&]() { EXPECT_EQ(0, counter++); }));
  tasks.add(evalLater([&]() {
    EXPECT_EQ(1, counter++);
    ZC_FAIL_ASSERT("example TaskSet failure") { break; }
  }));
  tasks.add(evalLater([&]() { EXPECT_EQ(2, counter++); }));

  auto ignore ZC_UNUSED =
      evalLater([&]() { ZC_FAIL_EXPECT("Promise without waiter shouldn't execute."); });

  evalLater([&]() { EXPECT_EQ(3, counter++); }).wait(waitScope);

  EXPECT_EQ(4, counter);
  EXPECT_EQ(1u, errorHandler.exceptionCount);
}

#if ZC_USE_FIBERS || !_WIN32
// These tests require either fibers or pthreads in order to limit the stack size. Currently we
// don't have a version that works on Windows without fibers, so skip the tests there.

inline size_t getSmallStackSize() {
#if !_WIN32
  // pthread_attr_setstacksize() requires a stack size of at least PTHREAD_STACK_MIN, which can
  // vary by platform.  We'll clamp that to a reasonable range for stack overflow tests, and skip
  // the pthread-based tests if we can't get it.
  return zc::max(16 * 1024, zc::min(256 * 1024, PTHREAD_STACK_MIN));
#else
  return 16 * 1024;
#endif
}

// Runs the given function in a context with a limited stack size.
template <typename Func>
void runWithStackLimit(size_t stackSize, Func&& func) {
  // We have a couple possible ways to test limited stacks.  We exercise all available methods, to
  // reduce the likelihood of breakage in less frequently tested configurations.
  //
  // Prefer testing stack limits with fibers first, because it manifests stack overflow failures
  // with a segmentation fault and stack, while pthreads just aborts without output.

#if ZC_USE_FIBERS
  if (!isLibcContextHandlingKnownBroken()) {
    EventLoop loop;
    WaitScope waitScope(loop);

    startFiber(stackSize, [&](WaitScope&) mutable { func(); }).wait(waitScope);
  }
#endif

#if !_WIN32
  pthread_attr_t attr;
  ZC_REQUIRE(0 == pthread_attr_init(&attr));
  ZC_DEFER(ZC_REQUIRE(0 == pthread_attr_destroy(&attr)));

  auto setStackSizeRetval = pthread_attr_setstacksize(&attr, stackSize);
  if (setStackSizeRetval == EINVAL) {
    ZC_LOG(WARNING,
           "This platform's pthread implementation does not support setting a small stack size. "
           "Skipping pthread-based stack overflow test.",
           stackSize, PTHREAD_STACK_MIN, setStackSizeRetval);
  } else {
    ZC_REQUIRE(0 == setStackSizeRetval);
    pthread_t thread;
    auto start = [](void* startArg) -> void* {
      EventLoop loop;
      WaitScope waitScope(loop);
      auto startFunc = reinterpret_cast<decltype(&func)>(startArg);
      (*startFunc)();
      return nullptr;
    };
    ZC_REQUIRE(0 == pthread_create(&thread, &attr, start, reinterpret_cast<void*>(&func)));
    ZC_REQUIRE(0 == pthread_join(thread, nullptr));
  }
#endif
}

TEST(Async, LargeTaskSetDestruction) {
  size_t stackSize = getSmallStackSize();

  runWithStackLimit(stackSize, [stackSize]() {
    ErrorHandlerImpl errorHandler;
    TaskSet tasks(errorHandler);

    for (int i = 0; i < stackSize / sizeof(void*); i++) { tasks.add(zc::NEVER_DONE); }
  });
}

TEST(Async, LargeTaskSetDestructionExceptions) {
  size_t stackSize = getSmallStackSize();

  runWithStackLimit(stackSize, [stackSize]() {
    class ThrowingDestructor : public UnwindDetector {
    public:
      ~ThrowingDestructor() noexcept(false) {
        catchExceptionsIfUnwinding([]() { ZC_FAIL_ASSERT("ThrowingDestructor_exception"); });
      }
    };
    ErrorHandlerImpl errorHandler;
    Maybe<TaskSet> tasks;
    TaskSet& tasksRef = tasks.emplace(errorHandler);

    for (int i = 0; i < stackSize / sizeof(void*); i++) {
      tasksRef.add(zc::Promise<void>(zc::NEVER_DONE).attach(zc::heap<ThrowingDestructor>()));
    }

    ZC_EXPECT_THROW_MESSAGE("ThrowingDestructor_exception", { tasks = zc::none; });
  });
}

TEST(Async, LargeTaskSetClear) {
  size_t stackSize = getSmallStackSize();

  runWithStackLimit(stackSize, [stackSize]() {
    ErrorHandlerImpl errorHandler;
    TaskSet tasks(errorHandler);

    for (int i = 0; i < stackSize / sizeof(void*); i++) { tasks.add(zc::NEVER_DONE); }

    tasks.clear();
  });
}

TEST(Async, LargeTaskSetClearException) {
  size_t stackSize = getSmallStackSize();

  runWithStackLimit(stackSize, [stackSize]() {
    class ThrowingDestructor : public UnwindDetector {
    public:
      ~ThrowingDestructor() noexcept(false) {
        catchExceptionsIfUnwinding([]() { ZC_FAIL_ASSERT("ThrowingDestructor_exception"); });
      }
    };
    ErrorHandlerImpl errorHandler;
    TaskSet tasks(errorHandler);

    for (int i = 0; i < stackSize / sizeof(void*); i++) {
      tasks.add(zc::Promise<void>(zc::NEVER_DONE).attach(zc::heap<ThrowingDestructor>()));
    }

    ZC_EXPECT_THROW_MESSAGE("ThrowingDestructor_exception", { tasks.clear(); });
  });
}

#endif  // ZC_USE_FIBERS || !_WIN32

TEST(Async, TaskSet) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool destroyed = false;

  {
    ErrorHandlerImpl errorHandler;
    TaskSet tasks(errorHandler);

    tasks.add(zc::Promise<void>(zc::NEVER_DONE).attach(zc::defer([&]() {
      // During cancellation, append another task!
      // It had better be canceled too!
      tasks.add(zc::Promise<void>(zc::READY_NOW)
                    .then([]() { ZC_FAIL_EXPECT("shouldn't get here"); },
                          [](auto) { ZC_FAIL_EXPECT("shouldn't get here"); })
                    .attach(zc::defer([&]() { destroyed = true; })));
    })));
  }

  ZC_EXPECT(destroyed);

  // Give a chance for the "shouldn't get here" asserts to execute, if the event is still running,
  // which it shouldn't be.
  waitScope.poll();
}

TEST(Async, TaskSetOnEmpty) {
  EventLoop loop;
  WaitScope waitScope(loop);
  ErrorHandlerImpl errorHandler;
  TaskSet tasks(errorHandler);

  ZC_EXPECT(tasks.isEmpty());

  auto paf = newPromiseAndFulfiller<void>();
  tasks.add(zc::mv(paf.promise));
  tasks.add(yield());

  ZC_EXPECT(!tasks.isEmpty());

  auto promise = tasks.onEmpty();
  ZC_EXPECT(!promise.poll(waitScope));
  ZC_EXPECT(!tasks.isEmpty());

  paf.fulfiller->fulfill();
  ZC_ASSERT(promise.poll(waitScope));
  ZC_EXPECT(tasks.isEmpty());
  promise.wait(waitScope);
}

ZC_TEST("TaskSet::clear()") {
  EventLoop loop;
  WaitScope waitScope(loop);

  class ClearOnError : public TaskSet::ErrorHandler {
  public:
    TaskSet* tasks;
    void taskFailed(zc::Exception&& exception) override {
      ZC_EXPECT(exception.getDescription().endsWith("example TaskSet failure"));
      tasks->clear();
    }
  };

  ClearOnError errorHandler;
  TaskSet tasks(errorHandler);
  errorHandler.tasks = &tasks;

  auto doTest = [&](auto&& causeClear) {
    ZC_EXPECT(tasks.isEmpty());

    uint count = 0;
    tasks.add(zc::Promise<void>(zc::READY_NOW).attach(zc::defer([&]() { ++count; })));
    tasks.add(zc::Promise<void>(zc::NEVER_DONE).attach(zc::defer([&]() { ++count; })));
    tasks.add(zc::Promise<void>(zc::NEVER_DONE).attach(zc::defer([&]() { ++count; })));

    auto onEmpty = tasks.onEmpty();
    ZC_EXPECT(!onEmpty.poll(waitScope));
    ZC_EXPECT(count == 1);
    ZC_EXPECT(!tasks.isEmpty());

    causeClear();
    ZC_EXPECT(tasks.isEmpty());
    onEmpty.wait(waitScope);
    ZC_EXPECT(count == 3);
  };

  // Try it where we just call clear() directly.
  doTest([&]() { tasks.clear(); });

  // Try causing clear() inside taskFailed(), ensuring that this is permitted.
  doTest([&]() {
    tasks.add(ZC_EXCEPTION(FAILED, "example TaskSet failure"));
    waitScope.poll();
  });
}

class DestructorDetector {
public:
  DestructorDetector(bool& setTrue) : setTrue(setTrue) {}
  ~DestructorDetector() { setTrue = true; }

private:
  bool& setTrue;
};

TEST(Async, Attach) {
  bool destroyed = false;

  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() {
                           EXPECT_FALSE(destroyed);
                           return 123;
                         }).attach(zc::heap<DestructorDetector>(destroyed));

  promise = promise.then([&](int i) {
    EXPECT_TRUE(destroyed);
    return i + 321;
  });

  EXPECT_FALSE(destroyed);
  EXPECT_EQ(444, promise.wait(waitScope));
  EXPECT_TRUE(destroyed);
}

TEST(Async, EagerlyEvaluate) {
  bool called = false;

  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = Promise<void>(READY_NOW).then([&]() { called = true; });
  yield().wait(waitScope);

  EXPECT_FALSE(called);

  promise = promise.eagerlyEvaluate(nullptr);

  yield().wait(waitScope);

  EXPECT_TRUE(called);
}

TEST(Async, Detach) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool ran1 = false;
  bool ran2 = false;
  bool ran3 = false;

  {
    // let returned promise be destroyed (canceled)
    auto ignore ZC_UNUSED = evalLater([&]() { ran1 = true; });
  }
  evalLater([&]() { ran2 = true; }).detach([](zc::Exception&&) { ADD_FAILURE(); });
  evalLater([]() {
    ZC_FAIL_ASSERT("foo") { break; }
  }).detach([&](zc::Exception&& e) { ran3 = true; });

  EXPECT_FALSE(ran1);
  EXPECT_FALSE(ran2);
  EXPECT_FALSE(ran3);

  yield().wait(waitScope);

  EXPECT_FALSE(ran1);
  EXPECT_TRUE(ran2);
  EXPECT_TRUE(ran3);
}

class DummyEventPort : public EventPort {
public:
  bool runnable = false;
  int callCount = 0;

  bool wait() override { ZC_FAIL_ASSERT("Nothing to wait for."); }
  bool poll() override { return false; }
  void setRunnable(bool runnable) override {
    this->runnable = runnable;
    ++callCount;
  }
};

TEST(Async, SetRunnable) {
  DummyEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  EXPECT_FALSE(port.runnable);
  EXPECT_EQ(0, port.callCount);

  {
    auto promise = yield().eagerlyEvaluate(nullptr);

    EXPECT_TRUE(port.runnable);
    loop.run(1);
    EXPECT_FALSE(port.runnable);
    EXPECT_EQ(2, port.callCount);

    promise.wait(waitScope);
    EXPECT_FALSE(port.runnable);
    EXPECT_EQ(4, port.callCount);
  }

  {
    auto paf = newPromiseAndFulfiller<void>();
    auto promise = paf.promise.then([]() {}).eagerlyEvaluate(nullptr);
    EXPECT_FALSE(port.runnable);

    auto promise2 = yield().eagerlyEvaluate(nullptr);
    paf.fulfiller->fulfill();

    EXPECT_TRUE(port.runnable);
    loop.run(1);
    EXPECT_TRUE(port.runnable);
    loop.run(10);
    EXPECT_FALSE(port.runnable);

    promise.wait(waitScope);
    EXPECT_FALSE(port.runnable);

    EXPECT_EQ(8, port.callCount);
  }
}

TEST(Async, Poll) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<void>();
  ZC_ASSERT(!paf.promise.poll(waitScope));
  paf.fulfiller->fulfill();
  ZC_ASSERT(paf.promise.poll(waitScope));
  paf.promise.wait(waitScope);
}

ZC_TEST("Maximum turn count during wait scope poll is enforced") {
  EventLoop loop;
  WaitScope waitScope(loop);
  ErrorHandlerImpl errorHandler;
  TaskSet tasks(errorHandler);

  auto evaluated1 = false;
  tasks.add(evalLater([&]() { evaluated1 = true; }));

  auto evaluated2 = false;
  tasks.add(evalLater([&]() { evaluated2 = true; }));

  auto evaluated3 = false;
  tasks.add(evalLater([&]() { evaluated3 = true; }));

  uint count;

  // Check that only events up to a maximum are resolved:
  count = waitScope.poll(2);
  ZC_ASSERT(count == 2);
  ZC_EXPECT(evaluated1);
  ZC_EXPECT(evaluated2);
  ZC_EXPECT(!evaluated3);

  // Get the last remaining event in the queue:
  count = waitScope.poll(1);
  ZC_ASSERT(count == 1);
  ZC_EXPECT(evaluated3);

  // No more events:
  count = waitScope.poll(1);
  ZC_ASSERT(count == 0);
}

ZC_TEST("exclusiveJoin both events complete simultaneously") {
  // Previously, if both branches of an exclusiveJoin() completed simultaneously, then the parent
  // event could be armed twice. This is an error, but the exact results of this error depend on
  // the parent PromiseNode type. One case where it matters is ArrayJoinPromiseNode, which counts
  // events and decides it is done when it has received exactly the number of events expected.

  EventLoop loop;
  WaitScope waitScope(loop);

  auto builder = zc::heapArrayBuilder<zc::Promise<uint>>(2);
  builder.add(zc::Promise<uint>(123).exclusiveJoin(zc::Promise<uint>(456)));
  builder.add(zc::NEVER_DONE);
  auto joined = zc::joinPromises(builder.finish());

  ZC_EXPECT(!joined.poll(waitScope));
}

#if ZC_USE_FIBERS
ZC_TEST("start a fiber") {
  if (isLibcContextHandlingKnownBroken()) return;

  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<int>();

  Promise<StringPtr> fiber =
      startFiber(65536, [promise = zc::mv(paf.promise)](WaitScope& fiberScope) mutable {
        int i = promise.wait(fiberScope);
        ZC_EXPECT(i == 123);
        return "foo"_zc;
      });

  ZC_EXPECT(!fiber.poll(waitScope));

  paf.fulfiller->fulfill(123);

  ZC_ASSERT(fiber.poll(waitScope));
  ZC_EXPECT(fiber.wait(waitScope) == "foo");
}

ZC_TEST("fiber promise chaining") {
  if (isLibcContextHandlingKnownBroken()) return;

  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<int>();
  bool ran = false;

  Promise<int> fiber =
      startFiber(65536, [promise = zc::mv(paf.promise), &ran](WaitScope& fiberScope) mutable {
        ran = true;
        return zc::mv(promise);
      });

  ZC_EXPECT(!ran);
  ZC_EXPECT(!fiber.poll(waitScope));
  ZC_EXPECT(ran);

  paf.fulfiller->fulfill(123);

  ZC_ASSERT(fiber.poll(waitScope));
  ZC_EXPECT(fiber.wait(waitScope) == 123);
}

ZC_TEST("throw from a fiber") {
  if (isLibcContextHandlingKnownBroken()) return;

  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<void>();

  Promise<void> fiber =
      startFiber(65536, [promise = zc::mv(paf.promise)](WaitScope& fiberScope) mutable {
        promise.wait(fiberScope);
        ZC_FAIL_EXPECT("wait() should have thrown");
      });

  ZC_EXPECT(!fiber.poll(waitScope));

  paf.fulfiller->reject(ZC_EXCEPTION(FAILED, "test exception"));

  ZC_ASSERT(fiber.poll(waitScope));
  ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("test exception", fiber.wait(waitScope));
}

#if !__MINGW32__ || __MINGW64__
// This test fails on MinGW 32-bit builds due to a compiler bug with exceptions + fibers:
//     https://sourceforge.net/p/mingw-w64/bugs/835/
ZC_TEST("cancel a fiber") {
  if (isLibcContextHandlingKnownBroken()) return;

  EventLoop loop;
  WaitScope waitScope(loop);

  // When exceptions are disabled we can't wait() on a non-void promise that throws.
  auto paf = newPromiseAndFulfiller<void>();

  bool exited = false;
  bool canceled = false;

  {
    Promise<StringPtr> fiber = startFiber(
        65536, [promise = zc::mv(paf.promise), &exited, &canceled](WaitScope& fiberScope) mutable {
          ZC_DEFER(exited = true);
          try {
            promise.wait(fiberScope);
          } catch (zc::CanceledException) {
            canceled = true;
            throw;
          }
          return "foo"_zc;
        });

    ZC_EXPECT(!fiber.poll(waitScope));
    ZC_EXPECT(!exited);
    ZC_EXPECT(!canceled);
  }

  ZC_EXPECT(exited);
  ZC_EXPECT(canceled);
}
#endif

ZC_TEST("fiber pool") {
  if (isLibcContextHandlingKnownBroken()) return;

  EventLoop loop;
  WaitScope waitScope(loop);
  FiberPool pool(65536);

  int* i1_local = nullptr;
  int* i2_local = nullptr;

  auto run = [&]() mutable {
    auto paf1 = newPromiseAndFulfiller<int>();
    auto paf2 = newPromiseAndFulfiller<int>();

    {
      Promise<int> fiber1 =
          pool.startFiber([&, promise = zc::mv(paf1.promise)](WaitScope& scope) mutable {
            int i = promise.wait(scope);
            ZC_EXPECT(i == 123);
            if (i1_local == nullptr) {
              i1_local = &i;
            } else {
#if !ZC_HAS_COMPILER_FEATURE(address_sanitizer)
              // Verify that the stack variable is in the exact same spot as before.
              // May not work under ASAN as the instrumentation to detect stack-use-after-return can
              // change the address.
              ZC_ASSERT(i1_local == &i);
#endif
            }
            return i;
          });
      {
        Promise<int> fiber2 =
            pool.startFiber([&, promise = zc::mv(paf2.promise)](WaitScope& scope) mutable {
              int i = promise.wait(scope);
              ZC_EXPECT(i == 456);
              if (i2_local == nullptr) {
                i2_local = &i;
              } else {
#if !ZC_HAS_COMPILER_FEATURE(address_sanitizer)
                ZC_ASSERT(i2_local == &i);
#endif
              }
              return i;
            });

        ZC_EXPECT(!fiber1.poll(waitScope));
        ZC_EXPECT(!fiber2.poll(waitScope));

        ZC_EXPECT(pool.getFreelistSize() == 0);

        paf2.fulfiller->fulfill(456);

        ZC_EXPECT(!fiber1.poll(waitScope));
        ZC_ASSERT(fiber2.poll(waitScope));
        ZC_EXPECT(fiber2.wait(waitScope) == 456);

        ZC_EXPECT(pool.getFreelistSize() == 1);
      }

      paf1.fulfiller->fulfill(123);

      ZC_ASSERT(fiber1.poll(waitScope));
      ZC_EXPECT(fiber1.wait(waitScope) == 123);

      ZC_EXPECT(pool.getFreelistSize() == 2);
    }
  };
  run();
  ZC_ASSERT(i1_local != nullptr);
  ZC_ASSERT(i2_local != nullptr);
  // run the same thing and reuse the fibers
  run();
}

bool onOurStack(char* p) {
  // If p points less than 64k away from a random stack variable, then it must be on the same
  // stack, since we never allocate stacks smaller than 64k.
#if ZC_HAS_COMPILER_FEATURE(address_sanitizer)
  // The stack-use-after-return detection mechanism breaks our ability to check this, so don't.
  return true;
#else
  char c;
  ptrdiff_t diff = p - &c;
  return diff < 65536 && diff > -65536;
#endif
}

bool notOnOurStack(char* p) {
  // Opposite of onOurStack(), except returns true if the check can't be performed.
#if ZC_HAS_COMPILER_FEATURE(address_sanitizer)
  // The stack-use-after-return detection mechanism breaks our ability to check this, so don't.
  return true;
#else
  return !onOurStack(p);
#endif
}

ZC_TEST("fiber pool runSynchronously()") {
  if (isLibcContextHandlingKnownBroken()) return;

  FiberPool pool(65536);

  {
    char c;
    ZC_EXPECT(onOurStack(&c));  // sanity check...
  }

  char* ptr1 = nullptr;
  char* ptr2 = nullptr;

  pool.runSynchronously([&]() {
    char c;
    ptr1 = &c;
  });
  ZC_ASSERT(ptr1 != nullptr);

  pool.runSynchronously([&]() {
    char c;
    ptr2 = &c;
  });
  ZC_ASSERT(ptr2 != nullptr);

#if !ZC_HAS_COMPILER_FEATURE(address_sanitizer)
  // Should have used the same stack both times, so local var would be in the same place.
  // Under ASAN, the stack-use-after-return detection correctly fires on this, so we skip the check.
  ZC_EXPECT(ptr1 == ptr2);
#endif

  // Should have been on a different stack from the main stack.
  ZC_EXPECT(notOnOurStack(ptr1));

  ZC_EXPECT_THROW_MESSAGE("test exception",
                          pool.runSynchronously([&]() { ZC_FAIL_ASSERT("test exception"); }));
}

ZC_TEST("fiber pool limit") {
  if (isLibcContextHandlingKnownBroken()) return;

  FiberPool pool(65536);

  pool.setMaxFreelist(1);

  zc::MutexGuarded<uint> state;

  char* ptr1;
  char* ptr2;

  // Run some code that uses two stacks in separate threads at the same time.
  {
    zc::Thread thread([&]() noexcept {
      auto lock = state.lockExclusive();
      lock.wait([](uint val) { return val == 1; });

      pool.runSynchronously([&]() {
        char c;
        ptr2 = &c;

        *lock = 2;
        lock.wait([](uint val) { return val == 3; });
      });
    });

    ([&]() noexcept {
      auto lock = state.lockExclusive();

      pool.runSynchronously([&]() {
        char c;
        ptr1 = &c;

        *lock = 1;
        lock.wait([](uint val) { return val == 2; });
      });

      *lock = 3;
    })();
  }

  ZC_EXPECT(pool.getFreelistSize() == 1);

  // We expect that if we reuse a stack from the pool, it will be the last one that exited, which
  // is the one from the thread.
  pool.runSynchronously([&]() {
    ZC_EXPECT(onOurStack(ptr2));
    ZC_EXPECT(notOnOurStack(ptr1));

    ZC_EXPECT(pool.getFreelistSize() == 0);
  });

  ZC_EXPECT(pool.getFreelistSize() == 1);

  // Note that it would NOT work to try to allocate two stacks at the same time again and verify
  // that the second stack doesn't match the previously-deleted stack, because there's a high
  // likelihood that the new stack would be allocated in the same location.
}

#if __GNUC__ >= 12 && !__clang__
// The test below intentionally takes a pointer to a stack variable and stores it past the end
// of the function. This seems to trigger a warning in newer GCCs.
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif

ZC_TEST("run event loop on freelisted stacks") {
  if (isLibcContextHandlingKnownBroken()) return;

  FiberPool pool(65536);

  class MockEventPort : public EventPort {
  public:
    bool wait() override {
      char c;
      waitStack = &c;
      ZC_IF_SOME(f, fulfiller) {
        f->fulfill();
        fulfiller = zc::none;
      }
      return false;
    }
    bool poll() override {
      char c;
      pollStack = &c;
      ZC_IF_SOME(f, fulfiller) {
        f->fulfill();
        fulfiller = zc::none;
      }
      return false;
    }

    char* waitStack = nullptr;
    char* pollStack = nullptr;

    zc::Maybe<zc::Own<PromiseFulfiller<void>>> fulfiller;
  };

  MockEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);
  waitScope.runEventCallbacksOnStackPool(pool);

  {
    auto paf = newPromiseAndFulfiller<void>();
    port.fulfiller = zc::mv(paf.fulfiller);

    char* ptr1 = nullptr;
    char* ptr2 = nullptr;
    zc::evalLater([&]() {
      char c;
      ptr1 = &c;
      return zc::mv(paf.promise);
    })
        .then([&]() {
          char c;
          ptr2 = &c;
        })
        .wait(waitScope);

    ZC_EXPECT(ptr1 != nullptr);
    ZC_EXPECT(ptr2 != nullptr);
    ZC_EXPECT(port.waitStack != nullptr);
    ZC_EXPECT(port.pollStack == nullptr);

    // The event callbacks should have run on a different stack, but the wait should have been on
    // the main stack.
    ZC_EXPECT(notOnOurStack(ptr1));
    ZC_EXPECT(notOnOurStack(ptr2));
    ZC_EXPECT(onOurStack(port.waitStack));

    pool.runSynchronously([&]() {
      // This should run on the same stack where the event callbacks ran.
      ZC_EXPECT(onOurStack(ptr1));
      ZC_EXPECT(onOurStack(ptr2));
      ZC_EXPECT(notOnOurStack(port.waitStack));
    });
  }

  port.waitStack = nullptr;
  port.pollStack = nullptr;

  // Now try poll() instead of wait(). Note that since poll() doesn't block, we let it run on the
  // event stack.
  {
    auto paf = newPromiseAndFulfiller<void>();
    port.fulfiller = zc::mv(paf.fulfiller);

    char* ptr1 = nullptr;
    char* ptr2 = nullptr;
    auto promise = zc::evalLater([&]() {
                     char c;
                     ptr1 = &c;
                     return zc::mv(paf.promise);
                   }).then([&]() {
      char c;
      ptr2 = &c;
    });

    ZC_EXPECT(promise.poll(waitScope));

    ZC_EXPECT(ptr1 != nullptr);
    ZC_EXPECT(ptr2 == nullptr);  // didn't run because of lazy continuation evaluation
    ZC_EXPECT(port.waitStack == nullptr);
    ZC_EXPECT(port.pollStack != nullptr);

    // The event callback should have run on a different stack, and poll() should have run on
    // a separate stack too.
    ZC_EXPECT(notOnOurStack(ptr1));
    ZC_EXPECT(notOnOurStack(port.pollStack));

    pool.runSynchronously([&]() {
      // This should run on the same stack where the event callbacks ran.
      ZC_EXPECT(onOurStack(ptr1));
      ZC_EXPECT(onOurStack(port.pollStack));
    });
  }
}
#endif

ZC_TEST("retryOnDisconnect") {
  EventLoop loop;
  WaitScope waitScope(loop);

  {
    uint i = 0;
    auto promise = retryOnDisconnect([&]() -> Promise<int> {
      i++;
      return 123;
    });
    ZC_EXPECT(i == 0);
    ZC_EXPECT(promise.wait(waitScope) == 123);
    ZC_EXPECT(i == 1);
  }

  {
    uint i = 0;
    auto promise = retryOnDisconnect([&]() -> Promise<int> {
      if (i++ == 0) {
        return ZC_EXCEPTION(DISCONNECTED, "test disconnect");
      } else {
        return 123;
      }
    });
    ZC_EXPECT(i == 0);
    ZC_EXPECT(promise.wait(waitScope) == 123);
    ZC_EXPECT(i == 2);
  }

  {
    uint i = 0;
    auto promise = retryOnDisconnect([&]() -> Promise<int> {
      if (i++ <= 1) {
        return ZC_EXCEPTION(DISCONNECTED, "test disconnect", i);
      } else {
        return 123;
      }
    });
    ZC_EXPECT(i == 0);
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("test disconnect; i = 2",
                                        promise.ignoreResult().wait(waitScope));
    ZC_EXPECT(i == 2);
  }

  {
    // Test passing a reference to a function.
    struct Func {
      uint i = 0;
      Promise<int> operator()() {
        if (i++ == 0) {
          return ZC_EXCEPTION(DISCONNECTED, "test disconnect");
        } else {
          return 123;
        }
      }
    };
    Func func;

    auto promise = retryOnDisconnect(func);
    ZC_EXPECT(func.i == 0);
    ZC_EXPECT(promise.wait(waitScope) == 123);
    ZC_EXPECT(func.i == 2);
  }
}

#if (__GLIBC__ == 2 && __GLIBC_MINOR__ <= 17) || (__MINGW32__ && !__MINGW64__)
// manylinux2014-x86 doesn't seem to respect `alignas(16)`. I am guessing this is a glibc issue
// but I don't really know. It uses glibc 2.17, so testing for that and skipping the test makes
// CI work.
//
// MinGW 32-bit also mysteriously fails this test but I am not going to spend time figuring out
// why.
#else
ZC_TEST("capture weird alignment in continuation") {
  struct alignas(16) WeirdAlign {
    ~WeirdAlign() { ZC_EXPECT(reinterpret_cast<uintptr_t>(this) % 16 == 0); }
    int i;
  };

  EventLoop loop;
  WaitScope waitScope(loop);

  zc::Promise<void> p = zc::READY_NOW;

  WeirdAlign value = {123};
  WeirdAlign value2 = {456};
  auto p2 = p.then([value, value2]() -> WeirdAlign { return {value.i + value2.i}; });

  ZC_EXPECT(p2.wait(waitScope).i == 579);
}
#endif

ZC_TEST("constPromise") {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> p = constPromise<int, 123>();
  int i = p.wait(waitScope);
  ZC_EXPECT(i == 123);
}

ZC_TEST("EventLoopLocal") {
  static const EventLoopLocal<int> evLocalInt;
  static const EventLoopLocal<Own<Refcounted>> evLocalOwn;

  auto rc1 = zc::refcounted<Refcounted>();
  auto rc2 = zc::refcounted<Refcounted>();

  {
    EventLoop loop1, loop2;

    {
      WaitScope waitScope(loop1);
      *evLocalInt = 123;
      *evLocalOwn = zc::addRef(*rc1);
    }

    {
      WaitScope waitScope(loop2);
      *evLocalInt = 456;
      *evLocalOwn = zc::addRef(*rc2);
    }

    {
      WaitScope waitScope(loop1);
      ZC_EXPECT(*evLocalInt == 123);
      ZC_EXPECT(evLocalOwn->get() == rc1.get());
    }

    {
      WaitScope waitScope(loop2);
      ZC_EXPECT(*evLocalInt == 456);
      ZC_EXPECT(evLocalOwn->get() == rc2.get());
    }

    ZC_EXPECT(rc1->isShared());
    ZC_EXPECT(rc2->isShared());
  }

  // Destroying the event loop destoys all locals, so these are no longer shared.
  ZC_EXPECT(!rc1->isShared());
  ZC_EXPECT(!rc2->isShared());
}

}  // namespace
}  // namespace zc
