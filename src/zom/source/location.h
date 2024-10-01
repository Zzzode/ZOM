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

namespace zom {
namespace source {

class SourceLoc {
 public:
  SourceLoc() : value_(0) {}

  bool IsValid() const { return value_ != 0; }
  bool IsInvalid() const { return !IsValid(); }

  unsigned GetOpaqueValue() const { return value_; }
  static SourceLoc GetFromOpaqueValue(unsigned value) {
    SourceLoc loc;
    loc.value_ = value;
    return loc;
  }

  SourceLoc GetAdvancedLoc(unsigned offset) const {
    return GetFromOpaqueValue(GetOpaqueValue() + offset);
  }

  void Print(zc::OutputStream& os) const {
    zc::String result = zc::str("SourceLoc(");
    if (IsValid()) {
      result = zc::str(result, "file_id=", GetOpaqueValue() >> 24,
                       " offset=", GetOpaqueValue() & 0xFFFFFF);
    } else {
      result = zc::str(result, "invalid");
    }
    result = zc::str(result, ")");
    os.write(result.asBytes());
  }

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
  SourceRange(SourceLoc start, SourceLoc end) : start_(start), end_(end) {}

  SourceLoc start() const { return start_; }
  SourceLoc end() const { return end_; }

  bool IsValid() const { return start_.IsValid() && end_.IsValid(); }
  bool IsInvalid() const { return !IsValid(); }

  bool Contains(SourceLoc loc) const { return start_ <= loc && loc <= end_; }

  bool Overlaps(const SourceRange& other) const {
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

  void Print(zc::OutputStream& os) const {
    os.write(zc::StringPtr("SourceRange(").asBytes());
    start_.Print(os);
    os.write(zc::StringPtr(", ").asBytes());
    end_.Print(os);
    os.write(zc::StringPtr(")").asBytes());
  }

 private:
  SourceLoc start_;
  SourceLoc end_;
};

class CharSourceRange {
 public:
  CharSourceRange() = default;
  CharSourceRange(SourceLoc start, SourceLoc end, bool is_token_range = true)
      : start_(start), end_(end), is_token_range_(is_token_range) {}

  static CharSourceRange GetTokenRange(SourceLoc start, SourceLoc end) {
    return CharSourceRange(start, end, true);
  }

  static CharSourceRange GetCharRange(SourceLoc start, SourceLoc end) {
    return CharSourceRange(start, end, false);
  }

  SourceLoc start() const { return start_; }
  SourceLoc end() const { return end_; }
  bool IsTokenRange() const { return is_token_range_; }
  bool IsCharRange() const { return !is_token_range_; }

  SourceRange GetAsRange() const { return SourceRange(start_, end_); }

 private:
  SourceLoc start_;
  SourceLoc end_;
  bool is_token_range_;
};

}  // namespace source
}  // namespace zom

#endif  // ZOM_SOURCE_LOCATION_H