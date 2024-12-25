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

#ifndef ZOM_SOURCE_LOCATION_H_
#define ZOM_SOURCE_LOCATION_H_

#include "src/zc/core/io.h"
#include "src/zc/core/source-location.h"
#include "src/zc/core/string.h"

namespace zom {
namespace source {

class SourceLoc {
public:
  SourceLoc() : value(0) {}

  ZC_NODISCARD bool isValid() const { return value != 0; }
  ZC_NODISCARD bool isInvalid() const { return !isValid(); }

  ZC_NODISCARD unsigned getOpaqueValue() const { return value; }
  static SourceLoc getFromOpaqueValue(const unsigned value) {
    SourceLoc loc;
    loc.value = value;
    return loc;
  }

  ZC_NODISCARD SourceLoc getAdvancedLoc(unsigned offset) const {
    return getFromOpaqueValue(getOpaqueValue() + offset);
  }

  ZC_NODISCARD zc::String toString() const {
    if (isValid()) {
      return zc::str("SourceLoc(file_id=", getOpaqueValue() >> 24,
                     " offset=", getOpaqueValue() & 0xFFFFFF, ")");
    }
    return zc::str("SourceLoc(invalid)");
  }

  void print(zc::OutputStream& os) const { os.write(toString().asBytes()); }

  bool operator==(const SourceLoc& rhs) const { return value == rhs.value; }
  bool operator!=(const SourceLoc& rhs) const { return !operator==(rhs); }
  bool operator<(const SourceLoc& rhs) const { return value < rhs.value; }
  bool operator<=(const SourceLoc& rhs) const { return value <= rhs.value; }
  bool operator>(const SourceLoc& rhs) const { return value > rhs.value; }
  bool operator>=(const SourceLoc& rhs) const { return value >= rhs.value; }

private:
  unsigned value;
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

  ZC_NODISCARD zc::String toString() const {
    return zc::str("SourceRange(", start.toString(), ", ", end.toString(), ")");
  }

  void print(zc::OutputStream& os) const { os.write(toString().asBytes()); }

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

  ZC_NODISCARD zc::String toString() const {
    return zc::str("CharSourceRange(", start.toString(), ", ", end.toString(), ", ",
                   isTokenRange ? "token" : "char", ")");
  }

private:
  SourceLoc start;
  SourceLoc end;
  bool isTokenRange{false};

  static SourceLoc computeEnd(SourceLoc start, unsigned length) {
    ZC_IREQUIRE(!start.isInvalid(), "Invalid start location.");
    ZC_IREQUIRE(length > 0, "Length must be greater than zero.");

    unsigned startvalue = start.getOpaqueValue();
    unsigned endvalue = startvalue + length;

    // Check for overflow
    ZC_IREQUIRE(endvalue >= startvalue, "Overflow in length calculation.");

    // Check if the end position is within valid range
    // Assuming SourceLoc uses 24 bits for offset
    ZC_IREQUIRE(endvalue <= 0xFFFFFF, "End position exceeds valid range.");

    return SourceLoc::getFromOpaqueValue(endvalue);
  }
};

// 使用 zc::SourceLocation 来表示编译时的源代码位置
using CompileTimeSourceLocation = zc::SourceLocation;

inline zc::String ZC_STRINGIFY(const CompileTimeSourceLocation& loc) {
  return zc::str("File: ", loc.fileName, ", Function: ", loc.function, ", Line: ", loc.lineNumber,
                 ", Column: ", loc.columnNumber);
}

}  // namespace source
}  // namespace zom

#endif  // ZOM_SOURCE_LOCATION_H_
