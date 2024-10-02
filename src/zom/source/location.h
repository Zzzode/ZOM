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

#include "src/zc/base/io.h"
#include "src/zc/strings/string.h"
#include "src/zc/utility/source_location.h"

namespace zom {
namespace source {

class SourceLoc {
 public:
  SourceLoc() : value_(0) {}

  ZC_NODISCARD bool IsValid() const { return value_ != 0; }
  ZC_NODISCARD bool IsInvalid() const { return !IsValid(); }

  ZC_NODISCARD unsigned GetOpaqueValue() const { return value_; }
  static SourceLoc GetFromOpaqueValue(const unsigned value) {
    SourceLoc loc;
    loc.value_ = value;
    return loc;
  }

  ZC_NODISCARD SourceLoc GetAdvancedLoc(unsigned offset) const {
    return GetFromOpaqueValue(GetOpaqueValue() + offset);
  }

  ZC_NODISCARD zc::String ToString() const {
    if (IsValid()) {
      return zc::str("SourceLoc(file_id=", GetOpaqueValue() >> 24,
                     " offset=", GetOpaqueValue() & 0xFFFFFF, ")");
    }
    return zc::str("SourceLoc(invalid)");
  }

  void Print(zc::OutputStream& os) const { os.write(ToString().asBytes()); }

  bool operator==(const SourceLoc& rhs) const { return value_ == rhs.value_; }
  bool operator!=(const SourceLoc& rhs) const { return !operator==(rhs); }
  bool operator<(const SourceLoc& rhs) const { return value_ < rhs.value_; }
  bool operator<=(const SourceLoc& rhs) const { return value_ <= rhs.value_; }
  bool operator>(const SourceLoc& rhs) const { return value_ > rhs.value_; }
  bool operator>=(const SourceLoc& rhs) const { return value_ >= rhs.value_; }

 private:
  unsigned value_;
};

class SourceRange {
 public:
  SourceRange() = default;
  SourceRange(const SourceLoc start, const SourceLoc end)
      : start_(start), end_(end) {}

  ZC_NODISCARD SourceLoc start() const { return start_; }
  ZC_NODISCARD SourceLoc end() const { return end_; }

  ZC_NODISCARD bool IsValid() const {
    return start_.IsValid() && end_.IsValid();
  }
  ZC_NODISCARD bool IsInvalid() const { return !IsValid(); }

  ZC_NODISCARD bool Contains(SourceLoc loc) const {
    return start_ <= loc && loc <= end_;
  }

  ZC_NODISCARD bool Overlaps(const SourceRange& other) const {
    return Contains(other.start()) || other.Contains(start_);
  }

  void Widen(SourceRange other) {
    if (other.start() < start_) {
      start_ = other.start();
    }
    if (other.end() > end_) {
      end_ = other.end();
    }
  }

  ZC_NODISCARD zc::String ToString() const {
    return zc::str("SourceRange(", start_.ToString(), ", ", end_.ToString(),
                   ")");
  }

  void Print(zc::OutputStream& os) const { os.write(ToString().asBytes()); }

 private:
  SourceLoc start_;
  SourceLoc end_;
};

class CharSourceRange {
 public:
  CharSourceRange() = default;
  CharSourceRange(const SourceLoc start, const SourceLoc end,
                  const bool is_token_range = true)
      : start_(start), end_(end), is_token_range_(is_token_range) {
    ZC_IREQUIRE(start <= end,
                "Start location must be before or equal to end location.");
  }

  CharSourceRange(const SourceLoc start, const unsigned length,
                  const bool is_token_range = true)
      : start_(start),
        end_(ComputeEnd(start, length)),
        is_token_range_(is_token_range) {}

  static CharSourceRange GetTokenRange(const SourceLoc start,
                                       const SourceLoc end) {
    return CharSourceRange(start, end, true);
  }

  static CharSourceRange GetCharRange(const SourceLoc start,
                                      const SourceLoc end) {
    return CharSourceRange(start, end, false);
  }
  ZC_NODISCARD bool Contains(SourceLoc loc) const {
    return start_ <= loc && loc < end_;
  }

  ZC_NODISCARD unsigned length() const {
    if (start_.IsInvalid() || end_.IsInvalid()) {
      return 0;
    }
    return end_.GetOpaqueValue() - start_.GetOpaqueValue();
  }
  ZC_NODISCARD SourceLoc start() const { return start_; }
  ZC_NODISCARD SourceLoc end() const { return end_; }
  ZC_NODISCARD bool IsTokenRange() const { return is_token_range_; }
  ZC_NODISCARD bool IsCharRange() const { return !is_token_range_; }

  ZC_NODISCARD SourceRange GetAsRange() const {
    return SourceRange(start_, end_);
  }

  ZC_NODISCARD zc::String ToString() const {
    return zc::str("CharSourceRange(", start_.ToString(), ", ", end_.ToString(),
                   ", ", is_token_range_ ? "token" : "char", ")");
  }

 private:
  SourceLoc start_;
  SourceLoc end_;
  bool is_token_range_{false};

  static SourceLoc ComputeEnd(SourceLoc start, unsigned length) {
    ZC_IREQUIRE(!start.IsInvalid(), "Invalid start location.");
    ZC_IREQUIRE(length > 0, "Length must be greater than zero.");

    unsigned start_value = start.GetOpaqueValue();
    unsigned end_value = start_value + length;

    // Check for overflow
    ZC_IREQUIRE(end_value >= start_value, "Overflow in length calculation.");

    // Check if the end position is within valid range
    // Assuming SourceLoc uses 24 bits for offset
    ZC_IREQUIRE(end_value <= 0xFFFFFF, "End position exceeds valid range.");

    return SourceLoc::GetFromOpaqueValue(end_value);
  }
};

// 使用 zc::SourceLocation 来表示编译时的源代码位置
using CompileTimeSourceLocation = zc::SourceLocation;

inline zc::String ZC_STRINGIFY(const CompileTimeSourceLocation& loc) {
  return zc::str("File: ", loc.fileName, ", Function: ", loc.function,
                 ", Line: ", loc.lineNumber, ", Column: ", loc.columnNumber);
}

}  // namespace source
}  // namespace zom

#endif  // ZOM_SOURCE_LOCATION_H_