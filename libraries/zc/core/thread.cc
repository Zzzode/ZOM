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

#include "zc/core/thread.h"

#include "zc/core/debug.h"

#if _WIN32
#include <windows.h>

#include "zc/core/windows-sanity.h"
#else
#include <pthread.h>
#include <signal.h>
#endif

namespace zc {

#if _WIN32

Thread::Thread(Function<void()> func) : state(new ThreadState(zc::mv(func))) {
  threadHandle = CreateThread(nullptr, 0, &runThread, state, 0, nullptr);
  if (threadHandle == nullptr) {
    state->unref();
    ZC_FAIL_ASSERT("CreateThread failed.");
  }
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    ZC_DEFER(state->unref());

    ZC_ASSERT(WaitForSingleObject(threadHandle, INFINITE) != WAIT_FAILED);

    ZC_IF_SOME(e, state->exception) {
      Exception ecopy = zc::mv(e);
      state->exception = zc::none;  // don't complain of uncaught exception when deleting
      zc::throwRecoverableException(zc::mv(ecopy));
    }
  }
}

void Thread::detach() {
  ZC_ASSERT(CloseHandle(threadHandle));
  detached = true;
}

#else  // _WIN32

Thread::Thread(Function<void()> func) : state(new ThreadState(zc::mv(func))) {
  static_assert(sizeof(threadId) >= sizeof(pthread_t),
                "pthread_t is larger than a long long on your platform.  Please port.");

  int pthreadResult =
      pthread_create(reinterpret_cast<pthread_t*>(&threadId), nullptr, &runThread, state);
  if (pthreadResult != 0) {
    state->unref();
    ZC_FAIL_SYSCALL("pthread_create", pthreadResult);
  }
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    ZC_DEFER(state->unref());

    int pthreadResult = pthread_join(*reinterpret_cast<pthread_t*>(&threadId), nullptr);
    if (pthreadResult != 0) {
      ZC_FAIL_SYSCALL("pthread_join", pthreadResult) { break; }
    }

    ZC_IF_SOME(e, state->exception) {
      Exception ecopy = zc::mv(e);
      state->exception = zc::none;  // don't complain of uncaught exception when deleting
      zc::throwRecoverableException(zc::mv(ecopy));
    }
  }
}

void Thread::sendSignal(int signo) {
  int pthreadResult = pthread_kill(*reinterpret_cast<pthread_t*>(&threadId), signo);
  if (pthreadResult != 0) {
    ZC_FAIL_SYSCALL("pthread_kill", pthreadResult) { break; }
  }
}

void Thread::detach() {
  int pthreadResult = pthread_detach(*reinterpret_cast<pthread_t*>(&threadId));
  if (pthreadResult != 0) {
    ZC_FAIL_SYSCALL("pthread_detach", pthreadResult) { break; }
  }
  detached = true;
  state->unref();
}

#endif  // _WIN32, else

Thread::ThreadState::ThreadState(Function<void()> func)
    : func(zc::mv(func)),
      initializer(getExceptionCallback().getThreadInitializer()),
      exception(zc::none),
      refcount(2) {}

void Thread::ThreadState::unref() {
#if _MSC_VER && !defined(__clang__)
  if (_InterlockedDecrement(&refcount) == 0) {
#else
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE) == 0) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif

    ZC_IF_SOME(e, exception) {
      // If the exception is still present in ThreadState, this must be a detached thread, so
      // the exception will never be rethrown. We should at least log it.
      //
      // We need to run the thread initializer again before we log anything because the main
      // purpose of the thread initializer is to set up a logging callback.
      initializer([&]() { ZC_LOG(ERROR, "uncaught exception thrown by detached thread", e); });
    }

    delete this;
  }
}

#if _WIN32
DWORD Thread::runThread(void* ptr) {
#else
void* Thread::runThread(void* ptr) {
#endif
  ThreadState* state = reinterpret_cast<ThreadState*>(ptr);
  ZC_IF_SOME(exception,
             zc::runCatchingExceptions([&]() { state->initializer(zc::mv(state->func)); })) {
    state->exception = zc::mv(exception);
  }
  state->unref();
  return 0;
}

}  // namespace zc
