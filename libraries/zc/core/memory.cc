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

#include "zc/core/memory.h"

#include <stdlib.h>

#include "zc/core/debug.h"

namespace zc {

namespace {

inline void requireValidAlignment(size_t alignment) {
  ZC_REQUIRE(alignment > 0 && (alignment & (alignment - 1)) == 0,
             "MemoryResource alignment must be a non-zero power of two");
}

}  // namespace

void* MemoryResource::allocate(size_t size, size_t alignment) {
  requireValidAlignment(alignment);
  return doAllocate(size, alignment);
}

void MemoryResource::deallocate(void* pointer, size_t size, size_t alignment) {
  ZC_REQUIRE(pointer != nullptr, "MemoryResource cannot deallocate a null pointer");
  requireValidAlignment(alignment);
  doDeallocate(pointer, size, alignment);
}

void* MemoryResource::doAllocate(size_t size, size_t alignment) {
  size_t effectiveAlignment = zc::max(alignment, alignof(void*));
  size_t overhead = _::checkedAllocationAdd(sizeof(void*), effectiveAlignment - 1);
  size_t allocationSize = _::checkedAllocationAdd(size, overhead);
  void* allocation = operator new(allocationSize);
  uintptr_t alignedAddress = _::alignAllocationAddress(
      reinterpret_cast<uintptr_t>(allocation) + sizeof(void*), effectiveAlignment);
  auto* aligned = reinterpret_cast<void*>(alignedAddress);
  *(reinterpret_cast<void**>(aligned) - 1) = allocation;
  return aligned;
}

void MemoryResource::doDeallocate(void* pointer, size_t size, size_t alignment) {
  operator delete(*(reinterpret_cast<void**>(pointer) - 1));
}

CountingMemoryResource::CountingMemoryResource(MemoryResource& upstream) : upstream(upstream) {}

size_t CountingMemoryResource::currentAllocatedBytes() const { return currentBytes; }

size_t CountingMemoryResource::peakAllocatedBytes() const { return peakBytes; }

void* CountingMemoryResource::doAllocate(size_t size, size_t alignment) {
  ZC_REQUIRE(size <= static_cast<size_t>(zc::maxValue) - currentBytes,
             "CountingMemoryResource live-byte counter overflow");
  void* result = upstream.allocate(size, alignment);
  currentBytes += size;
  peakBytes = zc::max(peakBytes, currentBytes);
  return result;
}

void CountingMemoryResource::doDeallocate(void* pointer, size_t size, size_t alignment) {
  ZC_REQUIRE(size <= currentBytes, "CountingMemoryResource live-byte counter underflow");
  upstream.deallocate(pointer, size, alignment);
  currentBytes -= size;
}

const NullDisposer NullDisposer::instance = NullDisposer();

namespace _ {

#if ZC_ASSERT_PTR_COUNTERS

void atomicPtrCounterAssertionFailed(char const* reason) {
  ZC_FAIL_ASSERT("ptr counter contract violated", reason);

  // Really make sure we abort.
  ZC_KNOWN_UNREACHABLE(abort());
}

#endif  // ZC_ASSERT_PTR_COUNTERS

void throwWrongDisposerError() {
  ZC_FAIL_REQUIRE("When disowning an object, disposer must be equal to Own's disposer");
}

}  // namespace _

}  // namespace zc
