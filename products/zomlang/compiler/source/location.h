// Copyright (c) 2024 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/io.h"
#include "zc/core/source-location.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace source {

class SourceManager;

class SourceLoc {
public:
  SourceLoc() : ptr(nullptr) {}
  explicit SourceLoc(const zc::byte* p) : ptr(p) {}

  ZC_NODISCARD bool isValid() const { return ptr != nullptr; }
  ZC_NODISCARD bool isInvalid() const { return !isValid(); }

  ZC_NODISCARD const zc::byte* getOpaqueValue() const { return ptr; }
  static SourceLoc getFromOpaqueValue(const zc::byte* ptr) { return SourceLoc(ptr); }

  ZC_NODISCARD SourceLoc getAdvancedLoc(unsigned offset) const {
    return getFromOpaqueValue(getOpaqueValue() + offset);
  }

  ZC_NODISCARD zc::String toString(SourceManager& sm, uint64_t& lastBufferId) const;
  void print(zc::OutputStream& os, SourceManager& sm) const;

  bool operator==(const SourceLoc& rhs) const { return ptr == rhs.ptr; }
  bool operator!=(const SourceLoc& rhs) const { return !operator==(rhs); }
  bool operator<(const SourceLoc& rhs) const { return ptr < rhs.ptr; }
  bool operator<=(const SourceLoc& rhs) const { return ptr <= rhs.ptr; }
  bool operator>(const SourceLoc& rhs) const { return ptr > rhs.ptr; }
  bool operator>=(const SourceLoc& rhs) const { return ptr >= rhs.ptr; }

private:
  const zc::byte* ptr;
};

class SourceRange {
public:
  SourceRange() = default;
  SourceRange(const SourceLoc start, const SourceLoc end) : start(start), end(end) {}

  ZC_NODISCARD SourceLoc getStart() const { return start; }
  ZC_NODISCARD SourceLoc getEnd() const { return end; }

  ZC_NODISCARD bool isValid() const { return start.isValid() && end.isValid(); }
  ZC_NODISCARD bool isInvalid() const { return !isValid(); }

  ZC_NODISCARD bool contains(SourceLoc loc) const { return start <= loc && loc <= end; }

  ZC_NODISCARD bool overlaps(const SourceRange& other) const {
    return contains(other.getStart()) || other.contains(start);
  }

  void widen(SourceRange other) {
    if (other.getStart() < start) { start = other.getStart(); }
    if (other.getEnd() > end) { end = other.getEnd(); }
  }

  ZC_NODISCARD zc::String toString(SourceManager& sm, uint64_t lastBufferId = ~0ULL) const {
    return zc::str("SourceRange(", start.toString(sm, lastBufferId), ", ",
                   end.toString(sm, lastBufferId), ")");
  }

  void print(zc::OutputStream& os, SourceManager& sm) const {
    constexpr uint64_t tmp = ~0ULL;
    os.write(toString(sm, tmp).asBytes());
  }

private:
  SourceLoc start;
  SourceLoc end;
};

class CharSourceRange {
public:
  CharSourceRange() = default;
  CharSourceRange(const SourceLoc start, const SourceLoc end, const bool isTokenRange = true)
      : start(start), end(end), isTokenRange(isTokenRange) {
    ZC_IREQUIRE(start <= end, "Start location must be before or equal to end location.");
  }

  CharSourceRange(const SourceLoc start, const unsigned length, const bool isTokenRange = true)
      : start(start), end(computeEnd(start, length)), isTokenRange(isTokenRange) {}

  static CharSourceRange getTokenRange(const SourceLoc start, const SourceLoc end) {
    return CharSourceRange(start, end, true);
  }

  static CharSourceRange getCharRange(const SourceLoc start, const SourceLoc end) {
    return CharSourceRange(start, end, false);
  }
  ZC_NODISCARD bool contains(SourceLoc loc) const { return start <= loc && loc < end; }

  ZC_NODISCARD unsigned length() const {
    if (start.isInvalid() || end.isInvalid()) { return 0; }
    return end.getOpaqueValue() - start.getOpaqueValue();
  }
  ZC_NODISCARD SourceLoc getStart() const { return start; }
  ZC_NODISCARD SourceLoc getEnd() const { return end; }
  ZC_NODISCARD bool getIsTokenRange() const { return isTokenRange; }
  ZC_NODISCARD bool getIsCharRange() const { return !isTokenRange; }

  ZC_NODISCARD SourceRange getAsRange() const { return SourceRange(start, end); }

  ZC_NODISCARD zc::String toString(SourceManager& sm, uint64_t lastBufferId = ~0ULL) const {
    return zc::str("CharSourceRange(", start.toString(sm, lastBufferId), ", ",
                   end.toString(sm, lastBufferId), ", ", isTokenRange ? "token" : "char", ")");
  }

  bool operator==(const CharSourceRange& other) const {
    return start == other.start && end == other.end;
  }
  bool operator!=(const CharSourceRange& other) const { return !operator==(other); }

private:
  SourceLoc start;
  SourceLoc end;
  bool isTokenRange{false};

  static SourceLoc computeEnd(const SourceLoc start, const unsigned length) {
    ZC_IREQUIRE(!start.isInvalid(), "Invalid start location.");
    ZC_IREQUIRE(length > 0, "Length must be greater than zero.");

    const zc::byte* startvalue = start.getOpaqueValue();
    const zc::byte* endvalue = startvalue + length;

    // Check only pointer overflows (such as reverse offsets)
    ZC_IREQUIRE(endvalue >= startvalue, "Overflow in length calculation.");

    return SourceLoc::getFromOpaqueValue(endvalue);
  }
};

/// Use zc::SourceLocation to indicate the source code location at compile time
using CompileTimeSourceLocation = zc::SourceLocation;

inline zc::String ZC_STRINGIFY(const CompileTimeSourceLocation& loc) {
  return zc::str("File: ", loc.fileName, ", Function: ", loc.function, ", Line: ", loc.lineNumber,
                 ", Column: ", loc.columnNumber);
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
