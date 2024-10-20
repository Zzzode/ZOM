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

#ifndef ZC_BASE_IO_H_
#define ZC_BASE_IO_H_

#include <cstddef>
#include <cstdint>

#include "src/zc/base/common.h"
#include "src/zc/base/exception.h"
#include "src/zc/containers/array.h"

ZC_BEGIN_HEADER

namespace zc {

// =======================================================================================
// Abstract interfaces

class InputStream {
public:
  virtual ~InputStream() noexcept(false);

  size_t read(ArrayPtr<byte> buffer, size_t minBytes);
  // Reads at least minBytes and at most buffer.size(), copying them into the
  // given buffer. Returns the size read. Throws an exception on errors.
  // Implemented in terms of tryRead().
  //
  // buffer.size() is the number of bytes the caller really wants, but minBytes
  // is the minimum amount needed by the caller before it can start doing useful
  // processing. If the stream returns less than buffer.size(), the caller will
  // usually call read() again later to get the rest. Returning less than
  // buffer.size() is useful when it makes sense for the caller to parallelize
  // processing with I/O.
  //
  // Never blocks if minBytes is zero. If minBytes is zero and buffer.size() is
  // non-zero, this may attempt a non-blocking read or may just return zero. To
  // force a read, use a non-zero minBytes. To detect EOF without throwing an
  // exception, use tryRead().
  //
  // If the InputStream can't produce minBytes, it MUST throw an exception, as
  // the caller is not expected to understand how to deal with partial reads.

  virtual size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) = 0;
  // Like read(), but may return fewer than minBytes on EOF.

  void read(ArrayPtr<byte> buffer) { read(buffer, buffer.size()); }
  // Convenience method for reading an exact number of bytes.

  virtual void skip(size_t bytes);
  // Skips past the given number of bytes, discarding them. The default
  // implementation read()s into a scratch buffer.

  String readAllText(uint64_t limit = zc::maxValue);
  Array<byte> readAllBytes(uint64_t limit = zc::maxValue);
  // Read until EOF and return as one big byte array or string. Throw an
  // exception if EOF is not seen before reading `limit` bytes.
  //
  // To prevent runaway memory allocation, consider using a more conservative
  // value for `limit` than the default, particularly on untrusted data streams
  // which may never see EOF.
};

class OutputStream {
public:
  virtual ~OutputStream() noexcept(false);

  virtual void write(ArrayPtr<const byte> data) = 0;
  // Always writes the full size. Throws exception on error.

  virtual void write(ArrayPtr<const ArrayPtr<const byte>> pieces);
  // Equivalent to `write()`ing each byte array in sequence, which is what the
  // default implementation does. Override if you can do something better, for
  // example use `writev()` to do the write in a single syscall.
};

class BufferedInputStream : public InputStream {
  // An input stream, which buffers some bytes in memory to reduce system call
  // overhead.
  // - OR -
  // An input stream that actually reads from some in-memory data structure and
  // wants to give its caller a direct pointer to that memory to potentially
  // avoid a copy.

public:
  virtual ~BufferedInputStream() noexcept(false);

  ArrayPtr<const byte> getReadBuffer();
  // Get a direct pointer into the read buffer, which contains the next bytes in
  // the input. If the caller consumes any bytes, it should then call skip() to
  // indicate this. This always returns a non-empty buffer or throws an
  // exception. Implemented in terms of tryGetReadBuffer().

  virtual ArrayPtr<const byte> tryGetReadBuffer() = 0;
  // Like getReadBuffer() but may return an empty buffer on EOF.
};

class BufferedOutputStream : public OutputStream {
  // An output stream, which buffers some bytes in memory to reduce system call
  // overhead.
  // - OR -
  // An output stream that actually writes into some in-memory data structure
  // and wants to give its caller a direct pointer to that memory to potentially
  // avoid a copy.

public:
  virtual ~BufferedOutputStream() noexcept(false);

  virtual ArrayPtr<byte> getWriteBuffer() = 0;
  // Get a direct pointer into the write buffer. The caller may choose to fill
  // in some prefix of this buffer and then pass it to write(), in which case
  // write() may avoid a copy. It is incorrect to pass to write any slice of
  // this buffer, which is not a prefix.
};

// =======================================================================================
// Buffered streams implemented as wrappers around regular streams

class BufferedInputStreamWrapper : public BufferedInputStream {
  // Implements BufferedInputStream in terms of an InputStream.
  //
  // Note that the underlying stream's position is unpredictable once the
  // wrapper is destroyed, unless the entire stream was consumed. To read a
  // predictable number of bytes in a buffered way without going over, you'd
  // need this wrapper to wrap some other wrapper which itself implements an
  // artificial EOF at the desired point. Such a stream should be trivial to
  // write but is not provided by the library at this time.

public:
  explicit BufferedInputStreamWrapper(InputStream& inner, ArrayPtr<byte> buffer = nullptr);
  // Creates a buffered stream wrapping the given non-buffered stream. No
  // guarantee is made about the position of the inner stream after a buffered
  // wrapper has been created unless the entire input is read.
  //
  // If the second parameter is non-null, the stream uses the given buffer
  // instead of allocating its own. This may improve performance if the buffer
  // can be reused.

  ZC_DISALLOW_COPY_AND_MOVE(BufferedInputStreamWrapper);
  ~BufferedInputStreamWrapper() noexcept(false);

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<const byte> tryGetReadBuffer() override;
  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override;
  void skip(size_t bytes) override;

private:
  InputStream& inner;
  Array<byte> ownedBuffer;
  ArrayPtr<byte> buffer;
  ArrayPtr<const byte> bufferAvailable;
};

class BufferedOutputStreamWrapper : public BufferedOutputStream {
  // Implements BufferedOutputStream in terms of an OutputStream. Note that
  // writes to the underlying stream may be delayed until flush() is called or
  // the wrapper is destroyed.

public:
  explicit BufferedOutputStreamWrapper(OutputStream& inner, ArrayPtr<byte> buffer = nullptr);
  // Creates a buffered stream wrapping the given non-buffered stream.
  //
  // If the second parameter is non-null, the stream uses the given buffer
  // instead of allocating its own. This may improve performance if the buffer
  // can be reused.

  ZC_DISALLOW_COPY_AND_MOVE(BufferedOutputStreamWrapper);
  ~BufferedOutputStreamWrapper() noexcept(false);

  void flush();
  // Force the wrapper to write any remaining bytes in its buffer to the inner
  // stream. Note that this only flushes this object's buffer; this object has
  // no idea how to flush any other buffers that may be present in the
  // underlying stream.

  // implements BufferedOutputStream ---------------------------------
  ArrayPtr<byte> getWriteBuffer() override;

  void write(ArrayPtr<const byte> data) override;

private:
  OutputStream& inner;
  Array<byte> ownedBuffer;
  ArrayPtr<byte> buffer;
  byte* bufferPos;
  UnwindDetector unwindDetector;
};

// =======================================================================================
// Array I/O

class ArrayInputStream : public BufferedInputStream {
public:
  explicit ArrayInputStream(ArrayPtr<const byte> array);
  ZC_DISALLOW_COPY_AND_MOVE(ArrayInputStream);
  ~ArrayInputStream() noexcept(false);

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<const byte> tryGetReadBuffer() override;
  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override;
  void skip(size_t bytes) override;

private:
  ArrayPtr<const byte> array;
};

class ArrayOutputStream : public BufferedOutputStream {
public:
  explicit ArrayOutputStream(ArrayPtr<byte> array);
  ZC_DISALLOW_COPY_AND_MOVE(ArrayOutputStream);
  ~ArrayOutputStream() noexcept(false);

  ArrayPtr<byte> getArray() {
    // Get the portion of the array which has been filled in.
    return arrayPtr(array.begin(), fillPos);
  }

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<byte> getWriteBuffer() override;

  void write(ArrayPtr<const byte> data) override;

private:
  ArrayPtr<byte> array;
  byte* fillPos;
};

class VectorOutputStream : public BufferedOutputStream {
public:
  explicit VectorOutputStream(size_t initialCapacity = 4096);
  ZC_DISALLOW_COPY_AND_MOVE(VectorOutputStream);
  ~VectorOutputStream() noexcept(false);

  ArrayPtr<byte> getArray() {
    // Get the portion of the array which has been filled in.
    return arrayPtr(vector.begin(), fillPos);
  }

  void clear() { fillPos = vector.begin(); }

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<byte> getWriteBuffer() override;

  void write(ArrayPtr<const byte> data) override;

private:
  Array<byte> vector;
  byte* fillPos;

  void grow(size_t minSize);
};

// =======================================================================================
// File descriptor I/O

class AutoCloseFd {
  // A wrapper around a file descriptor which automatically closes the
  // descriptor when destroyed. The wrapper supports move construction for
  // transferring ownership of the descriptor. If close() returns an error, the
  // destructor throws an exception, UNLESS the destructor is being called
  // during unwind from another exception, in which case the close error is
  // ignored.
  //
  // If your code is not exception-safe, you should not use AutoCloseFd. In
  // this case, you will have to call close() yourself and handle errors
  // appropriately.

public:
  AutoCloseFd() : fd(-1) {}
  AutoCloseFd(std::nullptr_t) : fd(-1) {}
  explicit AutoCloseFd(int fd) : fd(fd) {}
  AutoCloseFd(AutoCloseFd&& other) noexcept : fd(other.fd) { other.fd = -1; }
  ZC_DISALLOW_COPY(AutoCloseFd);
  ~AutoCloseFd() noexcept(false);

  AutoCloseFd& operator=(AutoCloseFd&& other) {
    AutoCloseFd old(zc::mv(*this));
    fd = other.fd;
    other.fd = -1;
    return *this;
  }

  AutoCloseFd& operator=(std::nullptr_t) {
    AutoCloseFd old(zc::mv(*this));
    return *this;
  }

  operator int() const { return fd; }
  int get() const { return fd; }

  operator bool() const = delete;
  // Deleting this operator prevents accidental use in boolean contexts, which
  // the int conversion operator above would otherwise allow.

  bool operator==(std::nullptr_t) { return fd < 0; }

  int release() {
    // Release ownership of an FD. Not recommended.
    int result = fd;
    fd = -1;
    return result;
  }

private:
  int fd;
};

inline auto ZC_STRINGIFY(const AutoCloseFd& fd)
    -> decltype(zc::toCharSequence(implicitCast<int>(fd))) {
  return zc::toCharSequence(implicitCast<int>(fd));
}

class FdInputStream : public InputStream {
  // An InputStream wrapping a file descriptor.

public:
  explicit FdInputStream(int fd) : fd(fd) {}
  explicit FdInputStream(AutoCloseFd fd) : fd(fd), autoclose(mv(fd)) {}
  ZC_DISALLOW_COPY_AND_MOVE(FdInputStream);
  ~FdInputStream() noexcept(false);

  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override;

  int getFd() const { return fd; }

private:
  int fd;
  AutoCloseFd autoclose;
};

class FdOutputStream : public OutputStream {
  // An OutputStream wrapping a file descriptor.

public:
  explicit FdOutputStream(int fd) : fd(fd) {}
  explicit FdOutputStream(AutoCloseFd fd) : fd(fd), autoclose(mv(fd)) {}
  ZC_DISALLOW_COPY_AND_MOVE(FdOutputStream);
  ~FdOutputStream() noexcept(false);

  void write(ArrayPtr<const byte> data) override;
  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override;

  inline int getFd() const { return fd; }

private:
  int fd;
  AutoCloseFd autoclose;
};

// =======================================================================================
// Win32 Handle I/O

#ifdef _WIN32

class AutoCloseHandle {
  // A wrapper around a Win32 HANDLE which automatically closes the handle when
  // destroyed. The wrapper supports move construction for transferring
  // ownership of the handle. If CloseHandle() returns an error, the destructor
  // throws an exception, UNLESS the destructor is being called during unwind
  // from another exception, in which case the close error is ignored.
  //
  // If your code is not exception-safe, you should not use AutoCloseHandle. In
  // this case you will have to call close() yourself and handle errors
  // appropriately.

public:
  AutoCloseHandle() : handle((void*)-1) {}
  AutoCloseHandle(std::nullptr_t) : handle((void*)-1) {}
  explicit AutoCloseHandle(void* handle) : handle(handle) {}
  AutoCloseHandle(AutoCloseHandle&& other) noexcept : handle(other.handle) {
    other.handle = (void*)-1;
  }
  ZC_DISALLOW_COPY(AutoCloseHandle);
  ~AutoCloseHandle() noexcept(false);

  AutoCloseHandle& operator=(AutoCloseHandle&& other) {
    AutoCloseHandle old(zc::mv(*this));
    handle = other.handle;
    other.handle = (void*)-1;
    return *this;
  }

  AutoCloseHandle& operator=(std::nullptr_t) {
    AutoCloseHandle old(zc::mv(*this));
    return *this;
  }

  operator void*() const { return handle; }
  void* get() const { return handle; }

  operator bool() const = delete;
  // Deleting this operator prevents accidental use in boolean contexts, which
  // the void* conversion operator above would otherwise allow.

  bool operator==(std::nullptr_t) { return handle != (void*)-1; }

  void* release() {
    // Release ownership of an FD. Not recommended.
    void* result = handle;
    handle = (void*)-1;
    return result;
  }

private:
  void* handle;  // -1 (aka INVALID_HANDLE_VALUE) if not valid.
};

class HandleInputStream : public InputStream {
  // An InputStream wrapping a Win32 HANDLE.

public:
  explicit HandleInputStream(void* handle) : handle(handle) {}
  explicit HandleInputStream(AutoCloseHandle handle) : handle(handle), autoclose(mv(handle)) {}
  ZC_DISALLOW_COPY_AND_MOVE(HandleInputStream);
  ~HandleInputStream() noexcept(false);

  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override;

private:
  void* handle;
  AutoCloseHandle autoclose;
};

class HandleOutputStream : public OutputStream {
  // An OutputStream wrapping a Win32 HANDLE.

public:
  explicit HandleOutputStream(void* handle) : handle(handle) {}
  explicit HandleOutputStream(AutoCloseHandle handle) : handle(handle), autoclose(mv(handle)) {}
  ZC_DISALLOW_COPY_AND_MOVE(HandleOutputStream);
  ~HandleOutputStream() noexcept(false);

  void write(ArrayPtr<const byte> data) override;

private:
  void* handle;
  AutoCloseHandle autoclose;
};

#endif  // _WIN32

}  // namespace zc

ZC_END_HEADER

#endif  // ZC_BASE_IO_H_