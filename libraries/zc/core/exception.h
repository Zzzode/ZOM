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

#pragma once

#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/core/windows-sanity.h"  // work-around macro conflict with `ERROR`

ZC_BEGIN_HEADER

namespace zc {

class ExceptionImpl;
template <typename T>
class Function;

class Exception {
  // Exception thrown in case of fatal errors.
  //
  // Actually, a subclass of this which also implements std::exception will be thrown, but we hide
  // that fact from the interface to avoid #including <exception>.

public:
  enum class Type {
    // What kind of failure?

    FAILED = 0,
    // Something went wrong. This is the usual error type. ZC_ASSERT and ZC_REQUIRE throw this
    // error type.

    OVERLOADED = 1,
    // The call failed because of a temporary lack of resources. This could be space resources
    // (out of memory, out of disk space) or time resources (request queue overflow, operation
    // timed out).
    //
    // The operation might work if tried again, but it should NOT be repeated immediately as this
    // may simply exacerbate the problem.

    DISCONNECTED = 2,
    // The call required communication over a connection that has been lost. The callee will need
    // to re-establish connections and try again.

    UNIMPLEMENTED = 3
    // The requested method is not implemented. The caller may wish to revert to a fallback
    // approach based on other methods.

    // IF YOU ADD A NEW VALUE:
    // - Update the stringifier.
    // - Update Cap'n Proto's RPC protocol's Exception.Type enum.
  };

  Exception(Type type, const char* file, int line, String description = nullptr) noexcept;
  Exception(Type type, String file, int line, String description = nullptr) noexcept;
  Exception(const Exception& other) noexcept;
  Exception(Exception&& other) = default;
  ~Exception() noexcept;

  const char* getFile() const { return file; }
  int getLine() const { return line; }
  Type getType() const { return type; }
  StringPtr getDescription() const { return description; }
  ArrayPtr<void* const> getStackTrace() const { return arrayPtr(trace, traceCount); }

  void setDescription(zc::String&& desc) { description = zc::mv(desc); }

  StringPtr getRemoteTrace() const { return remoteTrace; }
  void setRemoteTrace(zc::String&& value) { remoteTrace = zc::mv(value); }
  // Additional stack trace data originating from a remote server. If present, then
  // `getStackTrace()` only traces up until entry into the RPC system, and the remote trace
  // contains any trace information returned over the wire. This string is human-readable but the
  // format is otherwise unspecified.

  struct Context {
    // Describes a bit about what was going on when the exception was thrown.

    const char* file;
    int line;
    String description;
    Maybe<Own<Context>> next;

    Context(const char* file, int line, String&& description, Maybe<Own<Context>>&& next)
        : file(file), line(line), description(mv(description)), next(mv(next)) {}
    Context(const Context& other) noexcept;
  };

  inline Maybe<const Context&> getContext() const {
    ZC_IF_SOME(c, context) { return *c; }
    else { return zc::none; }
  }

  void wrapContext(const char* file, int line, String&& description);
  // Wraps the context in a new node.  This becomes the head node returned by getContext() -- it
  // is expected that contexts will be added in reverse order as the exception passes up the
  // callback stack.

  ZC_NOINLINE void extendTrace(uint ignoreCount, uint limit = zc::maxValue);
  // Append the current stack trace to the exception's trace, ignoring the first `ignoreCount`
  // frames (see `getStackTrace()` for discussion of `ignoreCount`).
  //
  // If `limit` is set, limit the number of frames added to the given number.

  ZC_NOINLINE void truncateCommonTrace();
  // Remove the part of the stack trace which the exception shares with the caller of this method.
  // This is used by the async library to remove the async infrastructure from the stack trace
  // before replacing it with the async trace.

  void addTrace(void* ptr);
  // Append the given pointer to the backtrace, if it is not already full. This is used by the
  // async library to trace through the promise chain that led to the exception.

  ZC_NOINLINE void addTraceHere();
  // Adds the location that called this method to the stack trace.

  using DetailTypeId = unsigned long long;
  struct Detail {
    DetailTypeId id;
    zc::Array<byte> value;
  };

  zc::Maybe<zc::ArrayPtr<const byte>> getDetail(DetailTypeId typeId) const;
  zc::ArrayPtr<const Detail> getDetails() const;
  void setDetail(DetailTypeId typeId, zc::Array<byte> value);
  zc::Maybe<zc::Array<byte>> releaseDetail(DetailTypeId typeId);
  // Details: Arbitrary extra information can be added to an exception. Applications can define
  // any kind of detail they want, but it must be serializable to bytes so that it can be logged
  // and transmitted over RPC.
  //
  // Every type of detail must have a unique ID, which is a 64-bit integer. It's suggested that
  // you use `capnp id` to generate these.
  //
  // It is expected that exceptions will rarely have more than one or two details, so the
  // implementation uses a flat array with O(n) lookup.
  //
  // The main use case for details is to be able to tunnel exceptions of a different type through
  // ZC / Cap'n Proto. In particular, Cloudflare Workers commonly has to convert a JavaScript
  // exception to ZC and back. The exception is serialized using V8 serialization.

private:
  String ownFile;
  const char* file;
  int line;
  Type type;
  String description;
  Maybe<Own<Context>> context;
  String remoteTrace;
  void* trace[32];
  uint traceCount;

  bool isFullTrace = false;
  // Is `trace` a full trace to the top of the stack (or as close as we could get before we ran
  // out of space)? If this is false, then `trace` is instead a partial trace covering just the
  // frames between where the exception was thrown and where it was caught.
  //
  // extendTrace() transitions this to true, and truncateCommonTrace() changes it back to false.
  //
  // In theory, an exception should only hold a full trace when it is in the process of being
  // thrown via the C++ exception handling mechanism -- extendTrace() is called before the throw
  // and truncateCommonTrace() after it is caught. Note that when exceptions propagate through
  // async promises, the trace is extended one frame at a time instead, so isFullTrace should
  // remain false.

  zc::Vector<Detail> details;

  friend class ExceptionImpl;
};

struct CanceledException {};
// This exception is thrown to force-unwind a stack in order to immediately cancel whatever that
// stack was doing. It is used in the implementation of fibers in particular. Application code
// should almost never catch this exception, unless you need to modify stack unwinding for some
// reason. zc::runCatchingExceptions() does not catch it.

StringPtr ZC_STRINGIFY(Exception::Type type);
String ZC_STRINGIFY(const Exception& e);

// =======================================================================================

enum class LogSeverity {
  INFO,     // Information describing what the code is up to, which users may request to see
            // with a flag like `--verbose`.  Does not indicate a problem.  Not printed by
            // default; you must call setLogLevel(INFO) to enable.
  WARNING,  // A problem was detected but execution can continue with correct output.
  ERROR,    // Something is wrong, but execution can continue with garbage output.
  FATAL,    // Something went wrong, and execution cannot continue.
  DBG       // Temporary debug logging.  See ZC_DBG.

  // Make sure to update the stringifier if you add a new severity level.
};

StringPtr ZC_STRINGIFY(LogSeverity severity);

class ExceptionCallback {
  // If you don't like C++ exceptions, you may implement and register an ExceptionCallback in order
  // to perform your own exception handling.  For example, a reasonable thing to do is to have
  // onRecoverableException() set a flag indicating that an error occurred, and then check for that
  // flag just before writing to storage and/or returning results to the user.  If the flag is set,
  // discard whatever you have and return an error instead.
  //
  // ExceptionCallbacks must always be allocated on the stack.  When an exception is thrown, the
  // newest ExceptionCallback on the calling thread's stack is called.  The default implementation
  // of each method calls the next-oldest ExceptionCallback for that thread.  Thus the callbacks
  // behave a lot like try/catch blocks, except that they are called before any stack unwinding
  // occurs.

public:
  ExceptionCallback();
  ZC_DISALLOW_COPY_AND_MOVE(ExceptionCallback);
  virtual ~ExceptionCallback() noexcept(false);

  virtual void onRecoverableException(Exception&& exception);
  // Called when an exception has been raised, but the calling code has the ability to continue by
  // producing garbage output.  This method _should_ throw the exception, but is allowed to simply
  // return if garbage output is acceptable.
  //
  // The global default implementation throws an exception, unless we're currently in a destructor
  // unwinding due to another exception being thrown, in which case it logs an error and returns.

  virtual void onFatalException(Exception&& exception);
  // Called when an exception has been raised and the calling code cannot continue.  If this method
  // returns normally, abort() will be called.  The method must throw the exception to avoid
  // aborting.
  //
  // The global default implementation throws an exception.

  virtual void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                          String&& text);
  // Called when something wants to log some debug text.  `contextDepth` indicates how many levels
  // of context the message passed through; it may make sense to indent the message accordingly.
  //
  // The global default implementation writes the text to stderr.

  enum class StackTraceMode {
    FULL,
    // Stringifying a stack trace will attempt to determine source file and line numbers. This may
    // be expensive. For example, on Linux, this shells out to `addr2line`.
    //
    // This is the default in debug builds.

    ADDRESS_ONLY,
    // Stringifying a stack trace will only generate a list of code addresses.
    //
    // This is the default in release builds.

    NONE
    // Generating a stack trace will always return an empty array.
    //
    // This avoids ever unwinding the stack. On Windows in particular, the stack unwinding library
    // has been observed to be pretty slow, so exception-heavy code might benefit significantly
    // from this setting. (But exceptions should be rare...)
  };

  virtual StackTraceMode stackTraceMode();
  // Returns the current preferred stack trace mode.

  virtual Function<void(Function<void()>)> getThreadInitializer();
  // Called just before a new thread is spawned using zc::Thread. Returns a function which should
  // be invoked inside the new thread to initialize the thread's ExceptionCallback. The initializer
  // function itself receives, as its parameter, the thread's main function, which it must call.

protected:
  ExceptionCallback& next;

private:
  ExceptionCallback(ExceptionCallback& next);

  class RootExceptionCallback;
  friend ExceptionCallback& getExceptionCallback();

  friend class Thread;
};

ExceptionCallback& getExceptionCallback();
// Returns the current exception callback.

ZC_NOINLINE ZC_NORETURN(void throwFatalException(zc::Exception&& exception, uint ignoreCount = 0));
// Invoke the exception callback to throw the given fatal exception.  If the exception callback
// returns, abort.
//
// TODO(2.0): Rename this to `throwException()`.

ZC_NOINLINE void throwRecoverableException(zc::Exception&& exception, uint ignoreCount = 0);
// Invoke the exception callback to throw the given recoverable exception.  If the exception
// callback returns, return normally.
//
// TODO(2.0): Rename this to `throwExceptionUnlessUnwinding()`. (Or, can we fix the unwind problem
//   and be able to remove this entirely?)

// =======================================================================================

namespace _ {
class Runnable;
}

template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func);
// Executes the given function (usually, a lambda returning nothing) catching any exceptions that
// are thrown.  Returns the Exception if there was one, or null if the operation completed normally.
// Non-ZC exceptions will be wrapped.
//
// TODO(2.0): Remove this. Introduce ZC_CATCH() macro which uses getCaughtExceptionAsKj() to handle
//   exception coercion and stack trace management. Then use try/ZC_CATCH everywhere.

zc::Exception getCaughtExceptionAsKj();
// Call from the catch block of a try/catch to get a `zc::Exception` representing the exception
// that was caught, the same way that `zc::runCatchingExceptions` would when catching an exception.
// This is sometimes useful if `runCatchingExceptions()` doesn't quite fit your use case. You can
// call this from any catch block, including `catch (...)`.
//
// Some exception types will actually be rethrown by this function, rather than returned. The most
// common example is `CanceledException`, whose purpose is to unwind the stack and is not meant to
// be caught.

class UnwindDetector {
  // Utility for detecting when a destructor is called due to unwind.  Useful for:
  // - Avoiding throwing exceptions in this case, which would terminate the program.
  // - Detecting whether to commit or roll back a transaction.
  //
  // To use this class, either inherit privately from it or declare it as a member.  The detector
  // works by comparing the exception state against that when the constructor was called, so for
  // an object that was actually constructed during exception unwind, it will behave as if no
  // unwind is taking place.  This is usually the desired behavior.

public:
  UnwindDetector();

  bool isUnwinding() const;
  // Returns true if the current thread is in a stack unwind that it wasn't in at the time the
  // object was constructed.

  template <typename Func>
  void catchExceptionsIfUnwinding(Func&& func) const;
  // Runs the given function (e.g., a lambda).  If isUnwinding() is true, any exceptions are
  // caught and treated as secondary faults, meaning they are considered to be side-effects of the
  // exception that is unwinding the stack.  Otherwise, exceptions are passed through normally.

private:
  uint uncaughtCount;

  void catchThrownExceptionAsSecondaryFault() const;
};

template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func) {
  try {
    func();
    return zc::none;
  } catch (...) { return getCaughtExceptionAsKj(); }
}

template <typename Func>
void UnwindDetector::catchExceptionsIfUnwinding(Func&& func) const {
  if (isUnwinding()) {
    try {
      func();
    } catch (...) { catchThrownExceptionAsSecondaryFault(); }
  } else {
    func();
  }
}

#define ZC_ON_SCOPE_SUCCESS(code)                         \
  ::zc::UnwindDetector ZC_UNIQUE_NAME(_zcUnwindDetector); \
  ZC_DEFER(if (!ZC_UNIQUE_NAME(_zcUnwindDetector).isUnwinding()) { code; })
// Runs `code` if the current scope is exited normally (not due to an exception).

#define ZC_ON_SCOPE_FAILURE(code)                         \
  ::zc::UnwindDetector ZC_UNIQUE_NAME(_zcUnwindDetector); \
  ZC_DEFER(if (ZC_UNIQUE_NAME(_zcUnwindDetector).isUnwinding()) { code; })
// Runs `code` if the current scope is exited due to an exception.

// =======================================================================================

ZC_NOINLINE ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount);
// Attempt to get the current stack trace, returning a list of pointers to instructions. The
// returned array is a slice of `space`. Provide a larger `space` to get a deeper stack trace.
// If the platform doesn't support stack traces, returns an empty array.
//
// `ignoreCount` items will be truncated from the front of the trace. This is useful for chopping
// off a prefix of the trace that is uninteresting to the developer because it's just locations
// inside the debug infrastructure that is requesting the trace. Be careful to mark functions as
// ZC_NOINLINE if you intend to count them in `ignoreCount`. Note that, unfortunately, the
// ignored entries will still waste space in the `space` array (and the returned array's `begin()`
// is never exactly equal to `space.begin()` due to this effect, even if `ignoreCount` is zero
// since `getStackTrace()` needs to ignore its own internal frames).

String stringifyStackTrace(ArrayPtr<void* const>);
// Convert the stack trace to a string with file names and line numbers. This may involve executing
// suprocesses.

String stringifyStackTraceAddresses(ArrayPtr<void* const> trace);
StringPtr stringifyStackTraceAddresses(ArrayPtr<void* const> trace, ArrayPtr<char> scratch);
// Construct a string containing just enough information about a stack trace to be able to convert
// it to file and line numbers later using offline tools. This produces a sequence of
// space-separated code location identifiers. Each identifier may be an absolute address
// (hex number starting with 0x) or may be a module-relative address "<module>@0x<hex>". The
// latter case is preferred when ASLR is in effect and has loaded different modules at different
// addresses.

String getStackTrace();
// Get a stack trace right now and stringify it. Useful for debugging.

void printStackTraceOnCrash();
// Registers signal handlers on common "crash" signals like SIGSEGV that will (attempt to) print
// a stack trace. You should call this as early as possible on program startup. Programs using
// ZC_MAIN get this automatically.

void resetCrashHandlers();
// Resets all signal handlers set by printStackTraceOnCrash().

zc::StringPtr trimSourceFilename(zc::StringPtr filename);
// Given a source code file name, trim off noisy prefixes like "src/" or
// "/ekam-provider/canonical/".

zc::String getCaughtExceptionType();
// Utility function which attempts to return the human-readable type name of the exception
// currently being thrown. This can be called inside a catch block, including a catch (...) block,
// for the purpose of error logging. This function is best-effort; on some platforms it may simply
// return "(unknown)".

class InFlightExceptionIterator {
  // A class that can be used to iterate over exceptions that are in-flight in the current thread,
  // meaning they are either uncaught, or caught by a catch block that is current executing.
  //
  // This is meant for debugging purposes, and the results are best-effort. The C++ standard
  // library does not provide any way to inspect uncaught exceptions, so this class can only
  // discover ZC exceptions thrown using throwFatalException() or throwRecoverableException().
  // All ZC code uses those two functions to throw exceptions, but if your own code uses a bare
  // `throw`, or if the standard library throws an exception, these cannot be inspected.
  //
  // This class is safe to use in a signal handler.

public:
  InFlightExceptionIterator();

  Maybe<const Exception&> next();

private:
  const Exception* ptr;
};

zc::Exception getDestructionReason(void* traceSeparator, zc::Exception::Type defaultType,
                                   const char* defaultFile, int defaultLine,
                                   zc::StringPtr defaultDescription);
// Returns an exception that attempts to capture why a destructor has been invoked. If a ZC
// exception is currently in-flight (see InFlightExceptionIterator), then that exception is
// returned. Otherwise, an exception is constructed using the current stack trace and the type,
// file, line, and description provided. In the latter case, `traceSeparator` is appended to the
// stack trace; this should be a pointer to some dummy symbol which acts as a separator between the
// original stack trace and any new trace frames added later.

zc::ArrayPtr<void* const> computeRelativeTrace(zc::ArrayPtr<void* const> trace,
                                               zc::ArrayPtr<void* const> relativeTo);
// Given two traces expected to have started from the same root, try to find the part of `trace`
// that is different from `relativeTo`, considering that either or both traces might be truncated.
//
// This is useful for debugging, when reporting several related traces at once.

void requireOnStack(void* ptr, zc::StringPtr description);
// Throw an exception if `ptr` does not appear to point to something near the top of the stack.
// Used as a safety check for types that must be stack-allocated, like ExceptionCallback.

}  // namespace zc

ZC_END_HEADER
