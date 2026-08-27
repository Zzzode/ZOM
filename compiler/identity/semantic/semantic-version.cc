// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/identity/semantic/semantic-version.h"

#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::identity {
namespace {

bool isDigit(char value) { return value >= '0' && value <= '9'; }

bool isAsciiLetter(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

bool isNumericIdentifier(zc::StringPtr input, size_t start, size_t end) {
  if (start == end) { return false; }
  for (size_t index = start; index < end; ++index) {
    if (!isDigit(input[index])) { return false; }
  }
  return true;
}

bool isCoreNumericIdentifier(zc::StringPtr input, size_t start, size_t end) {
  return isNumericIdentifier(input, start, end) && (end - start == 1 || input[start] != '0');
}

bool validateCore(zc::StringPtr input, size_t end) {
  size_t start = 0;
  size_t componentCount = 0;
  for (size_t index = 0; index <= end; ++index) {
    if (index != end && input[index] != '.') { continue; }
    if (!isCoreNumericIdentifier(input, start, index)) { return false; }
    ++componentCount;
    start = index + 1;
  }
  return componentCount == 3;
}

bool validateIdentifiers(zc::StringPtr input, size_t start, size_t end,
                         bool rejectNumericLeadingZero) {
  if (start == end) { return false; }
  size_t componentStart = start;
  for (size_t index = start; index <= end; ++index) {
    if (index != end && input[index] != '.') {
      const char current = input[index];
      if (!isAsciiLetter(current) && !isDigit(current) && current != '-') { return false; }
      continue;
    }
    if (componentStart == index) { return false; }
    if (rejectNumericLeadingZero && isNumericIdentifier(input, componentStart, index) &&
        index - componentStart > 1 && input[componentStart] == '0') {
      return false;
    }
    componentStart = index + 1;
  }
  return true;
}

bool isSemanticVersion(zc::StringPtr input) {
  if (input.size() == 0) { return false; }

  size_t coreEnd = input.size();
  for (size_t index = 0; index < input.size(); ++index) {
    if (input[index] == '-' || input[index] == '+') {
      coreEnd = index;
      break;
    }
  }
  if (!validateCore(input, coreEnd)) { return false; }

  size_t cursor = coreEnd;
  if (cursor < input.size() && input[cursor] == '-') {
    const size_t prereleaseStart = ++cursor;
    while (cursor < input.size() && input[cursor] != '+') { ++cursor; }
    if (!validateIdentifiers(input, prereleaseStart, cursor, true)) { return false; }
  }

  if (cursor < input.size()) {
    if (input[cursor] != '+') { return false; }
    const size_t buildStart = ++cursor;
    if (!validateIdentifiers(input, buildStart, input.size(), false)) { return false; }
    cursor = input.size();
  }
  return cursor == input.size();
}

}  // namespace

ResolvedVersion::ResolvedVersion(zc::String&& canonical) noexcept : value(zc::mv(canonical)) {}

zc::Maybe<ResolvedVersion> ResolvedVersion::fromCanonical(zc::StringPtr input) {
  if (!isSemanticVersion(input)) { return zc::none; }
  return ResolvedVersion(zc::heapString(input));
}

zc::Maybe<ResolvedVersion> ResolvedVersion::fromCanonical(zc::MemoryResource& resource,
                                                          zc::StringPtr input) {
  if (!isSemanticVersion(input)) { return zc::none; }
  return ResolvedVersion(zc::resourceHeapString(resource, input));
}

ResolvedVersion ResolvedVersion::clone() const { return ResolvedVersion(zc::heapString(value)); }

ResolvedVersion ResolvedVersion::clone(zc::MemoryResource& resource) const {
  return ResolvedVersion(zc::resourceHeapString(resource, value));
}

zc::StringPtr ResolvedVersion::text() const noexcept { return value; }

void ResolvedVersion::encode(CanonicalEncoder& encoder) const {
  encoder.encodeByteString(value.asBytes());
}

bool ResolvedVersion::operator==(const ResolvedVersion& other) const noexcept {
  return value == other.value;
}

bool ResolvedVersion::operator<(const ResolvedVersion& other) const noexcept {
  return value < other.value;
}

}  // namespace zomlang::compiler::identity
