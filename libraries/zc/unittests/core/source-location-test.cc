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

#include "zc/core/source-location.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zc {
namespace {

ZC_TEST("SourceLocation basic functionality") {
  // Test default construction and basic properties
  SourceLocation loc;

#if ZC_COMPILER_SUPPORTS_SOURCE_LOCATION
  // When compiler supports source location, we should get valid data
  ZC_EXPECT(loc.fileName != nullptr);
  ZC_EXPECT(loc.function != nullptr);
  ZC_EXPECT(loc.lineNumber > 0);
  // columnNumber might be 0 if not supported by compiler
#else
  // When compiler doesn't support source location, we get dummy values
  ZC_EXPECT(zc::StringPtr(loc.fileName) == zc::StringPtr("??"));
  ZC_EXPECT(zc::StringPtr(loc.function) == zc::StringPtr("??"));
  ZC_EXPECT(loc.lineNumber == 0);
  ZC_EXPECT(loc.columnNumber == 0);
#endif
}

ZC_TEST("SourceLocation stringify") {
  // Test the ZC_STRINGIFY function for SourceLocation
  SourceLocation loc;

  zc::String str = zc::str(loc);
  ZC_EXPECT(str.size() > 0);

  // The string should contain the expected format: fileName:lineNumber:columnNumber in function
  ZC_EXPECT(str.findFirst(':') != zc::none);           // Should contain colons
  ZC_EXPECT(str.asPtr().find(" in "_zc) != zc::none);  // Should contain " in "
}

#if ZC_COMPILER_SUPPORTS_SOURCE_LOCATION
ZC_TEST("SourceLocation equality") {
  // Test equality operator when compiler supports source location
  SourceLocation loc1;
  SourceLocation loc2;

  // These should be equal since they're created on consecutive lines in the same function
  // Note: This test might be fragile depending on compiler behavior
  ZC_EXPECT(loc1.fileName == loc2.fileName);  // Same file
  ZC_EXPECT(loc1.function == loc2.function);  // Same function
  // Line numbers will be different, so full equality will be false
  ZC_EXPECT(!(loc1 == loc2));  // Different lines
}

static SourceLocation getSourceLocationFromFunction() {
  return SourceLocation();  // This will capture the location of this line
}

ZC_TEST("SourceLocation from different function") {
  SourceLocation loc1;
  SourceLocation loc2 = getSourceLocationFromFunction();

  // These should have different function names
  ZC_EXPECT(zc::StringPtr(loc1.function) != zc::StringPtr(loc2.function));
  ZC_EXPECT(!(loc1 == loc2));
}
#endif

ZC_TEST("NoopSourceLocation") {
  // Test NoopSourceLocation functionality
  NoopSourceLocation noop;

  // Test that zc::str works with NoopSourceLocation
  zc::String str = zc::str(noop);
  ZC_EXPECT(str.size() == 0);  // Should return empty string
}

ZC_TEST("SourceLocation field access") {
  // Test direct access to SourceLocation fields
  SourceLocation loc;

  // Test that we can access all fields without crashing
  const char* fileName = loc.fileName;
  const char* function = loc.function;

  ZC_EXPECT(fileName != nullptr);
  ZC_EXPECT(function != nullptr);
  // lineNumber and columnNumber values depend on compiler support
  ZC_EXPECT(loc.lineNumber >= 0);
  ZC_EXPECT(loc.columnNumber >= 0);
}

}  // namespace
}  // namespace zc
