// Copyright (c) 2020 Cloudflare, Inc. and contributors
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

#include <zc/async/async.h>
#include <zc/core/array.h>
#include <zc/core/debug.h>
#include <zc/http/http.h>
#include <zc/ztest/test.h>

namespace zc {
namespace {

template <typename T>
Promise<zc::Decay<T>> identity(T&& value) {
  co_return zc::fwd<T>(value);
}
// Work around a bonkers MSVC ICE with a separate overload.
Promise<const char*> identity(const char* value) { co_return value; }

ZC_TEST("Identity coroutine") {
  EventLoop loop;
  WaitScope waitScope(loop);

  ZC_EXPECT(identity(123).wait(waitScope) == 123);
  ZC_EXPECT(*identity(zc::heap(456)).wait(waitScope) == 456);

  { auto p = identity("we can cancel the coroutine"); }
}

template <typename T>
Promise<T> simpleCoroutine(zc::Promise<T> result, zc::Promise<bool> dontThrow = true) {
  // TODO(cleanup): Storing the coroutine result in a variable to work around
  // https://developercommunity.visualstudio.com/t/certain-coroutines-cause-error-C7587:-/10311276,
  // which caused a compile error here. This was supposed to be resolved with version 17.9, but
  // appears to still be happening as of 17.9.6. Clean up once this has been fixed.
  auto resolved = co_await dontThrow;
  ZC_ASSERT(resolved);
  co_return co_await result;
}

ZC_TEST("Simple coroutine test") {
  EventLoop loop;
  WaitScope waitScope(loop);

  simpleCoroutine(zc::Promise<void>(zc::READY_NOW)).wait(waitScope);

  ZC_EXPECT(simpleCoroutine(zc::Promise<int>(123)).wait(waitScope) == 123);
}

struct Counter {
  size_t& wind;
  size_t& unwind;
  Counter(size_t& wind, size_t& unwind) : wind(wind), unwind(unwind) { ++wind; }
  ~Counter() { ++unwind; }
  ZC_DISALLOW_COPY_AND_MOVE(Counter);
};

zc::Promise<void> countAroundAwait(size_t& wind, size_t& unwind, zc::Promise<void> promise) {
  Counter counter1(wind, unwind);
  co_await promise;
  Counter counter2(wind, unwind);
  co_return;
};

ZC_TEST("co_awaiting initial immediate promises suspends even if event loop is empty and running") {
  // The coroutine PromiseNode implementation contains an optimization which allows us to avoid
  // suspending the coroutine and instead immediately call PromiseNode::get() and proceed with
  // execution, but only if the coroutine has suspended at least once. This test verifies that the
  // optimization is disabled for this initial suspension.

  EventLoop loop;
  WaitScope waitScope(loop);

  // The immediate-execution optimization is only enabled when the event loop is running, so use an
  // eagerly-evaluated evalLater() to perform the test from within the event loop. (If we didn't
  // eagerly-evaluate the promise, the result would be extracted after the loop finished, which
  // would disable the optimization anyway.)
  zc::evalLater([&]() {
    size_t wind = 0, unwind = 0;

    auto promise = zc::Promise<void>(zc::READY_NOW);
    auto coroPromise = countAroundAwait(wind, unwind, zc::READY_NOW);

    // `coro` has not completed.
    ZC_EXPECT(wind == 1);
    ZC_EXPECT(unwind == 0);
  })
      .eagerlyEvaluate(nullptr)
      .wait(waitScope);

  zc::evalLater([&]() {
    // If there are no background tasks in the queue, coroutines execute through an evalLater()
    // without suspending.

    size_t wind = 0, unwind = 0;
    bool evalLaterRan = false;

    auto promise = zc::evalLater([&]() { evalLaterRan = true; });
    auto coroPromise = countAroundAwait(wind, unwind, zc::mv(promise));

    ZC_EXPECT(evalLaterRan == false);
    ZC_EXPECT(wind == 1);
    ZC_EXPECT(unwind == 0);
  })
      .eagerlyEvaluate(nullptr)
      .wait(waitScope);
}

ZC_TEST("co_awaiting an immediate promise suspends if the event loop is not running") {
  // We only want to enable the immediate-execution optimization if the event loop is running, or
  // else a whole bunch of RPC tests break, because some .then()s get evaluated on promise
  // construction, before any .wait() call.

  EventLoop loop;
  WaitScope waitScope(loop);

  size_t wind = 0, unwind = 0;

  auto promise = zc::Promise<void>(zc::READY_NOW);
  auto coroPromise = countAroundAwait(wind, unwind, zc::READY_NOW);

  // In the previous test, this exact same code executed immediately because the event loop was
  // running.
  ZC_EXPECT(wind == 1);
  ZC_EXPECT(unwind == 0);
}

ZC_TEST("co_awaiting immediate promises suspends if the event loop is not empty") {
  // We want to make sure that we can still return to the event loop when we need to.

  EventLoop loop;
  WaitScope waitScope(loop);

  // The immediate-execution optimization is only enabled when the event loop is running, so use an
  // eagerly-evaluated evalLater() to perform the test from within the event loop. (If we didn't
  // eagerly-evaluate the promise, the result would be extracted after the loop finished.)
  zc::evalLater([&]() {
    size_t wind = 0, unwind = 0;

    // We need to enqueue an Event on the event loop to inhibit the immediate-execution
    // optimization. Creating and then immediately fulfilling an EagerPromiseNode is a convenient
    // way to do so.
    auto paf = newPromiseAndFulfiller<void>();
    paf.promise = paf.promise.eagerlyEvaluate(nullptr);
    paf.fulfiller->fulfill();

    auto promise = zc::Promise<void>(zc::READY_NOW);
    auto coroPromise = countAroundAwait(wind, unwind, zc::READY_NOW);

    // We didn't immediately extract the READY_NOW.
    ZC_EXPECT(wind == 1);
    ZC_EXPECT(unwind == 0);
  })
      .eagerlyEvaluate(nullptr)
      .wait(waitScope);

  zc::evalLater([&]() {
    size_t wind = 0, unwind = 0;
    bool evalLaterRan = false;

    // We need to enqueue an Event on the event loop to inhibit the immediate-execution
    // optimization. Creating and then immediately fulfilling an EagerPromiseNode is a convenient
    // way to do so.
    auto paf = newPromiseAndFulfiller<void>();
    paf.promise = paf.promise.eagerlyEvaluate(nullptr);
    paf.fulfiller->fulfill();

    auto promise = zc::evalLater([&]() { evalLaterRan = true; });
    auto coroPromise = countAroundAwait(wind, unwind, zc::mv(promise));

    // We didn't continue through the evalLater() promise, because the background promise's
    // continuation was next in the event loop's queue.
    ZC_EXPECT(evalLaterRan == false);
    // No Counter destructor has run.
    ZC_EXPECT(wind == 1);
    ZC_EXPECT(unwind == 0);
  })
      .eagerlyEvaluate(nullptr)
      .wait(waitScope);
}

ZC_TEST("Exceptions propagate through layered coroutines") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto throwy = simpleCoroutine(zc::Promise<int>(zc::NEVER_DONE), false);

  ZC_EXPECT_THROW_RECOVERABLE(FAILED, simpleCoroutine(zc::mv(throwy)).wait(waitScope));
}

ZC_TEST("Exceptions before the first co_await don't escape, but reject the promise") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto throwEarly = []() -> Promise<void> {
    ZC_FAIL_ASSERT("test exception");
#ifdef __GNUC__
// Yes, this `co_return` is unreachable. But without it, this function is no longer a coroutine.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunreachable-code"
#endif  // __GNUC__
    co_return;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__
  };

  auto throwy = throwEarly();

  ZC_EXPECT_THROW_RECOVERABLE(FAILED, throwy.wait(waitScope));
}

ZC_TEST("Coroutines can catch exceptions from co_await") {
  EventLoop loop;
  WaitScope waitScope(loop);

  zc::String description;

  auto tryCatch = [&](zc::Promise<void> promise) -> zc::Promise<zc::String> {
    try {
      co_await promise;
    } catch (const zc::Exception& exception) { co_return zc::str(exception.getDescription()); }
    ZC_FAIL_EXPECT("should have thrown");
    ZC_UNREACHABLE;
  };

  {
    // Immediately ready case.
    auto promise = zc::Promise<void>(ZC_EXCEPTION(FAILED, "catch me"));
    ZC_EXPECT(tryCatch(zc::mv(promise)).wait(waitScope) == "catch me");
  }

  {
    // Ready later case.
    auto promise =
        zc::evalLater([]() -> zc::Promise<void> { return ZC_EXCEPTION(FAILED, "catch me"); });
    ZC_EXPECT(tryCatch(zc::mv(promise)).wait(waitScope) == "catch me");
  }
}

ZC_TEST("Coroutines can be canceled while suspended") {
  EventLoop loop;
  WaitScope waitScope(loop);

  size_t wind = 0, unwind = 0;

  auto coro = [&](zc::Promise<int> promise) -> zc::Promise<void> {
    Counter counter1(wind, unwind);
    co_await zc::yield();
    Counter counter2(wind, unwind);
    co_await promise;
  };

  {
    auto neverDone = zc::Promise<int>(zc::NEVER_DONE);
    neverDone = neverDone.attach(zc::heap<Counter>(wind, unwind));
    auto promise = coro(zc::mv(neverDone));
    ZC_EXPECT(!promise.poll(waitScope));
  }

  // Stack variables on both sides of a co_await, plus coroutine arguments are destroyed.
  ZC_EXPECT(wind == 3);
  ZC_EXPECT(unwind == 3);
}

zc::Promise<void> deferredThrowCoroutine(zc::Promise<void> awaitMe) {
  ZC_DEFER(zc::throwFatalException(ZC_EXCEPTION(FAILED, "thrown during unwind")));
  co_await awaitMe;
  co_return;
};

ZC_TEST("Exceptions during suspended coroutine frame-unwind propagate via destructor") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto exception = ZC_ASSERT_NONNULL(
      zc::runCatchingExceptions([&]() { (void)deferredThrowCoroutine(zc::NEVER_DONE); }));

  ZC_EXPECT(exception.getDescription() == "thrown during unwind");
};

ZC_TEST("Exceptions during suspended coroutine frame-unwind do not cause a memory leak") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // We can't easily test for memory leaks without hooking operator new and delete. However, we can
  // arrange for the test to crash on failure, by having the coroutine suspend at a promise that we
  // later fulfill, thus arming the Coroutine's Event. If we fail to destroy the coroutine in this
  // state, EventLoop will throw on destruction because it can still see the Event in its list.

  auto exception = ZC_ASSERT_NONNULL(zc::runCatchingExceptions([&]() {
    auto paf = zc::newPromiseAndFulfiller<void>();

    auto coroPromise = deferredThrowCoroutine(zc::mv(paf.promise));

    // Arm the Coroutine's Event.
    paf.fulfiller->fulfill();

    // If destroying `coroPromise` does not run ~Event(), then ~EventLoop() will crash later.
  }));

  ZC_EXPECT(exception.getDescription() == "thrown during unwind");
};

ZC_TEST("Exceptions during completed coroutine frame-unwind propagate via returned Promise") {
  EventLoop loop;
  WaitScope waitScope(loop);

  {
    // First, prove that exceptions don't escape the destructor of a completed coroutine.
    auto promise = deferredThrowCoroutine(zc::READY_NOW);
    ZC_EXPECT(promise.poll(waitScope));
  }

  {
    // Next, prove that they show up via the returned Promise.
    auto promise = deferredThrowCoroutine(zc::READY_NOW);
    ZC_EXPECT_THROW_RECOVERABLE_MESSAGE("thrown during unwind", promise.wait(waitScope));
  }
}

ZC_TEST("Coroutine destruction exceptions are ignored if there is another exception in flight") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto exception = ZC_ASSERT_NONNULL(zc::runCatchingExceptions([&]() {
    auto promise = deferredThrowCoroutine(zc::NEVER_DONE);
    zc::throwFatalException(ZC_EXCEPTION(FAILED, "thrown before destroying throwy promise"));
  }));

  ZC_EXPECT(exception.getDescription() == "thrown before destroying throwy promise");
}

ZC_TEST("co_await only sees coroutine destruction exceptions if promise was not rejected") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // throwyDtorPromise is an immediate void promise that will throw when it's destroyed, which
  // we expect to be able to catch from a coroutine which co_awaits it.
  auto throwyDtorPromise = zc::Promise<void>(zc::READY_NOW).attach(zc::defer([]() {
    zc::throwFatalException(ZC_EXCEPTION(FAILED, "thrown during unwind"));
  }));

  // rejectedThrowyDtorPromise is a rejected promise. When co_awaited in a coroutine,
  // Awaiter::await_resume() will throw that exception for us to catch, but before we can catch it,
  // the temporary promise will be destroyed. The exception it throws during unwind will be ignored,
  // and the caller of the coroutine will see only the "thrown during execution" exception.
  auto rejectedThrowyDtorPromise =
      zc::evalNow([&]() -> zc::Promise<void> {
        zc::throwFatalException(ZC_EXCEPTION(FAILED, "thrown during execution"));
      }).attach(zc::defer([]() {
        zc::throwFatalException(ZC_EXCEPTION(FAILED, "thrown during unwind"));
      }));

  auto awaitPromise = [](zc::Promise<void> promise) -> zc::Promise<void> { co_await promise; };

  ZC_EXPECT_THROW_MESSAGE("thrown during unwind",
                          awaitPromise(zc::mv(throwyDtorPromise)).wait(waitScope));

  ZC_EXPECT_THROW_MESSAGE("thrown during execution",
                          awaitPromise(zc::mv(rejectedThrowyDtorPromise)).wait(waitScope));
}

#if !_MSC_VER && !__aarch64__
// TODO(msvc): This test relies on GetFunctorStartAddress, which is not supported on MSVC currently,
//   so skip the test. Note this is an ABI issue, so clang-cl is also not supported.
// TODO(someday): Test is flakey on arm64, depending on how it's compiled. I haven't had a chance to
//   investigate much, but noticed that it failed in a debug build, but passed in a local opt build.
ZC_TEST("Can trace through coroutines") {
  // This verifies that async traces, generated either from promises or from events, can see through
  // coroutines.
  //
  // This test may be a bit brittle because it depends on specific trace counts.

  // Enable stack traces, even in release mode.
  class EnableFullStackTrace : public ExceptionCallback {
  public:
    StackTraceMode stackTraceMode() override { return StackTraceMode::FULL; }
  };
  EnableFullStackTrace exceptionCallback;

  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<void>();

  // Get an async trace when the promise is fulfilled. We eagerlyEvaluate() to make sure the
  // continuation executes while the event loop is running.
  paf.promise = paf.promise
                    .then([]() {
                      void* scratch[16];
                      auto trace = getAsyncTrace(scratch);
                      // We expect one entry for waitImpl(), one for the coroutine, and one for this
                      // continuation. When building in debug mode with CMake, I observed this count
                      // can be 2. The missing frame is probably this continuation. Let's just
                      // expect a range.
                      auto count = trace.size();
                      ZC_EXPECT(0 < count && count <= 3);
                    })
                    .eagerlyEvaluate(nullptr);

  auto coroPromise = [&]() -> zc::Promise<void> { co_await paf.promise; }();

  {
    void* space[32]{};
    _::TraceBuilder builder(space);
    _::PromiseNode::from(coroPromise).tracePromise(builder, false);

    // One for the Coroutine PromiseNode, one for paf.promise.
    ZC_EXPECT(builder.finish().size() >= 2);
  }

  paf.fulfiller->fulfill();

  coroPromise.wait(waitScope);
}
#endif  // !_MSC_VER || defined(__clang__)

Promise<void> sendData(Promise<Own<NetworkAddress>> addressPromise) {
  auto address = co_await addressPromise;
  auto client = co_await address->connect();
  co_await client->write("foo"_zcb);
}

Promise<String> receiveDataCoroutine(Own<ConnectionReceiver> listener) {
  auto server = co_await listener->accept();
  char buffer[4]{};
  auto n = co_await server->read(buffer, 3, 4);
  ZC_EXPECT(3u == n);
  co_return heapString(buffer, n);
}

ZC_TEST("Simple network test with coroutine") {
  auto io = setupAsyncIo();
  auto& network = io.provider->getNetwork();

  Own<NetworkAddress> serverAddress = network.parseAddress("*", 0).wait(io.waitScope);
  Own<ConnectionReceiver> listener = serverAddress->listen();

  sendData(network.parseAddress("localhost", listener->getPort()))
      .detach([](Exception&& exception) { ZC_FAIL_EXPECT(exception); });

  String result = receiveDataCoroutine(zc::mv(listener)).wait(io.waitScope);

  ZC_EXPECT("foo" == result);
}

Promise<Own<AsyncIoStream>> httpClientConnect(AsyncIoContext& io) {
  auto addr = co_await io.provider->getNetwork().parseAddress("capnproto.org", 80);
  co_return co_await addr->connect();
}

Promise<void> httpClient(Own<AsyncIoStream> connection) {
  // Borrowed and rewritten from compat/http-test.c++.

  HttpHeaderTable table;
  auto client = newHttpClient(table, *connection);

  HttpHeaders headers(table);
  headers.set(HttpHeaderId::HOST, "capnproto.org");

  auto response = co_await client->request(HttpMethod::GET, "/", headers).response;
  ZC_EXPECT(response.statusCode / 100 == 3);
  auto location = ZC_ASSERT_NONNULL(response.headers->get(HttpHeaderId::LOCATION));
  ZC_EXPECT(location == "https://capnproto.org/");

  auto body = co_await response.body->readAllText();
}

ZC_TEST("HttpClient to capnproto.org with a coroutine") {
  auto io = setupAsyncIo();

  auto promise = httpClientConnect(io).then(
      [](Own<AsyncIoStream> connection) { return httpClient(zc::mv(connection)); },
      [](Exception&&) {
        ZC_LOG(WARNING, "skipping test because couldn't connect to capnproto.org");
      });

  promise.wait(io.waitScope);
}

// =======================================================================================
// coCapture() tests

ZC_TEST("Verify coCapture() functors can only be run once") {
  auto io = zc::setupAsyncIo();

  auto functor = coCapture([](zc::Timer& timer) -> zc::Promise<void> {
    co_await timer.afterDelay(1 * zc::MILLISECONDS);
  });

  auto promise = functor(io.lowLevelProvider->getTimer());
  ZC_EXPECT_THROW(FAILED, functor(io.lowLevelProvider->getTimer()));

  promise.wait(io.waitScope);
}

auto makeDelayedIntegerFunctor(size_t i) {
  return [i](zc::Timer& timer) -> zc::Promise<size_t> {
    co_await timer.afterDelay(1 * zc::MILLISECONDS);
    co_return i;
  };
}

ZC_TEST("Verify coCapture() with local scoped functors") {
  auto io = zc::setupAsyncIo();

  constexpr size_t COUNT = 100;
  zc::Vector<zc::Promise<size_t>> promises;
  for (size_t i = 0; i < COUNT; ++i) {
    auto functor = coCapture(makeDelayedIntegerFunctor(i));
    promises.add(functor(io.lowLevelProvider->getTimer()));
  }

  for (size_t i = COUNT; i > 0; --i) {
    auto j = i - 1;
    auto result = promises[j].wait(io.waitScope);
    ZC_REQUIRE(result == j);
  }
}

auto makeCheckThenDelayedIntegerFunctor(zc::Timer& timer, size_t i) {
  return [&timer, i](size_t val) -> zc::Promise<size_t> {
    ZC_REQUIRE(val == i);
    co_await timer.afterDelay(1 * zc::MILLISECONDS);
    co_return i;
  };
}

ZC_TEST("Verify coCapture() with continuation functors") {
  // This test usually works locally without `coCapture()()`. It does however, fail in
  // ASAN.
  auto io = zc::setupAsyncIo();

  constexpr size_t COUNT = 100;
  zc::Vector<zc::Promise<size_t>> promises;
  for (size_t i = 0; i < COUNT; ++i) {
    auto promise =
        io.lowLevelProvider->getTimer().afterDelay(1 * zc::MILLISECONDS).then([i]() { return i; });
    promise = promise.then(
        coCapture(makeCheckThenDelayedIntegerFunctor(io.lowLevelProvider->getTimer(), i)));
    promises.add(zc::mv(promise));
  }

  for (size_t i = COUNT; i > 0; --i) {
    auto j = i - 1;
    auto result = promises[j].wait(io.waitScope);
    ZC_REQUIRE(result == j);
  }
}

}  // namespace
}  // namespace zc
