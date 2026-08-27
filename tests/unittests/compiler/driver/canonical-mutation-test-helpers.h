// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::tests::canonical_mutation {

struct WireRange final {
  size_t begin;
  size_t end;
};

struct SequenceRange final {
  WireRange bytes;
  uint64_t count;
  uint32_t byteStringsPerElement;
};

inline uint64_t readUint64(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  ZC_REQUIRE(offset <= bytes.size() && bytes.size() - offset >= sizeof(uint64_t));
  uint64_t result = 0;
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    result = (result << 8U) | bytes[offset + index];
  }
  return result;
}

inline void writeUint64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  ZC_REQUIRE(offset <= bytes.size() && bytes.size() - offset >= sizeof(uint64_t));
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    const auto shift = static_cast<uint32_t>((sizeof(uint64_t) - index - 1) * 8);
    bytes[offset + index] = static_cast<uint8_t>(value >> shift);
  }
}

inline size_t payloadOffset(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr domain) {
  ZC_REQUIRE(bytes.size() > domain.size());
  ZC_REQUIRE(bytes.slice(0, domain.size()) == domain.asBytes());
  ZC_REQUIRE(bytes[domain.size()] == 0);
  return domain.size() + 1;
}

inline WireRange consumeByteString(zc::ArrayPtr<const uint8_t> bytes, size_t& cursor) {
  const size_t begin = cursor;
  const uint64_t size = readUint64(bytes, cursor);
  cursor += sizeof(uint64_t);
  ZC_REQUIRE(size <= bytes.size() - cursor);
  cursor += static_cast<size_t>(size);
  return WireRange{begin, cursor};
}

inline SequenceRange consumeSequence(zc::ArrayPtr<const uint8_t> bytes, size_t& cursor,
                                     uint32_t byteStringsPerElement = 1) {
  const size_t begin = cursor;
  const uint64_t count = readUint64(bytes, cursor);
  cursor += sizeof(uint64_t);
  ZC_REQUIRE(count <= bytes.size());
  for (uint64_t index = 0; index < count; ++index) {
    for (uint32_t field = 0; field < byteStringsPerElement; ++field) {
      static_cast<void>(consumeByteString(bytes, cursor));
    }
  }
  return SequenceRange{WireRange{begin, cursor}, count, byteStringsPerElement};
}

inline WireRange sequenceElement(zc::ArrayPtr<const uint8_t> bytes, const SequenceRange& sequence,
                                 uint64_t index) {
  ZC_REQUIRE(index < sequence.count);
  size_t cursor = sequence.bytes.begin + sizeof(uint64_t);
  for (uint64_t element = 0; element < sequence.count; ++element) {
    const size_t begin = cursor;
    for (uint32_t field = 0; field < sequence.byteStringsPerElement; ++field) {
      static_cast<void>(consumeByteString(bytes, cursor));
    }
    if (element == index) { return WireRange{begin, cursor}; }
  }
  ZC_FAIL_REQUIRE("sequence element is missing");
}

inline WireRange sequenceField(zc::ArrayPtr<const uint8_t> bytes, const SequenceRange& sequence,
                               uint64_t index, uint32_t field) {
  ZC_REQUIRE(index < sequence.count);
  ZC_REQUIRE(field < sequence.byteStringsPerElement);
  size_t cursor = sequenceElement(bytes, sequence, index).begin;
  for (uint32_t current = 0; current <= field; ++current) {
    const auto range = consumeByteString(bytes, cursor);
    if (current == field) { return range; }
  }
  ZC_FAIL_REQUIRE("sequence field is missing");
}

inline zc::Array<uint8_t> flipPayloadByte(zc::ArrayPtr<const uint8_t> bytes, WireRange range) {
  ZC_REQUIRE(range.end > range.begin + sizeof(uint64_t));
  auto result = zc::heapArray<uint8_t>(bytes);
  result[range.end - 1] ^= 0x01;
  return result;
}

inline zc::Array<uint8_t> flipByte(zc::ArrayPtr<const uint8_t> bytes, size_t offset) {
  ZC_REQUIRE(offset < bytes.size());
  auto result = zc::heapArray<uint8_t>(bytes);
  result[offset] ^= 0x01;
  return result;
}

inline zc::Array<uint8_t> withTrailingByte(zc::ArrayPtr<const uint8_t> bytes) {
  auto result = zc::heapArray<uint8_t>(bytes.size() + 1);
  for (size_t index = 0; index < bytes.size(); ++index) { result[index] = bytes[index]; }
  result[bytes.size()] = 0;
  return result;
}

inline zc::Array<uint8_t> setByteStringSize(zc::ArrayPtr<const uint8_t> bytes, WireRange field,
                                            uint64_t size) {
  auto result = zc::heapArray<uint8_t>(bytes);
  writeUint64(result.asPtr(), field.begin, size);
  return result;
}

inline zc::Array<uint8_t> setSequenceCount(zc::ArrayPtr<const uint8_t> bytes,
                                           const SequenceRange& sequence, uint64_t count) {
  auto result = zc::heapArray<uint8_t>(bytes);
  writeUint64(result.asPtr(), sequence.bytes.begin, count);
  return result;
}

inline zc::Array<uint8_t> duplicateFirstElement(zc::ArrayPtr<const uint8_t> bytes,
                                                const SequenceRange& sequence) {
  ZC_REQUIRE(sequence.count != 0);
  const auto first = sequenceElement(bytes, sequence, 0);
  zc::Vector<uint8_t> result(bytes.size() + first.end - first.begin);
  result.addAll(bytes.slice(0, first.end));
  result.addAll(bytes.slice(first.begin, first.end));
  result.addAll(bytes.slice(first.end, bytes.size()));
  auto array = result.releaseAsArray();
  writeUint64(array.asPtr(), sequence.bytes.begin, sequence.count + 1);
  return array;
}

inline zc::Array<uint8_t> removeFirstElement(zc::ArrayPtr<const uint8_t> bytes,
                                             const SequenceRange& sequence) {
  ZC_REQUIRE(sequence.count != 0);
  const auto first = sequenceElement(bytes, sequence, 0);
  zc::Vector<uint8_t> result(bytes.size() - (first.end - first.begin));
  result.addAll(bytes.slice(0, first.begin));
  result.addAll(bytes.slice(first.end, bytes.size()));
  auto array = result.releaseAsArray();
  writeUint64(array.asPtr(), sequence.bytes.begin, sequence.count - 1);
  return array;
}

inline zc::Array<uint8_t> swapFirstTwoElements(zc::ArrayPtr<const uint8_t> bytes,
                                               const SequenceRange& sequence) {
  ZC_REQUIRE(sequence.count >= 2);
  const auto first = sequenceElement(bytes, sequence, 0);
  const auto second = sequenceElement(bytes, sequence, 1);
  zc::Vector<uint8_t> result(bytes.size());
  result.addAll(bytes.slice(0, first.begin));
  result.addAll(bytes.slice(second.begin, second.end));
  result.addAll(bytes.slice(first.begin, first.end));
  result.addAll(bytes.slice(second.end, bytes.size()));
  return result.releaseAsArray();
}

inline zc::Array<uint8_t> swapAdjacentRanges(zc::ArrayPtr<const uint8_t> bytes, WireRange first,
                                             WireRange second) {
  ZC_REQUIRE(first.begin < first.end);
  ZC_REQUIRE(first.end == second.begin);
  ZC_REQUIRE(second.begin < second.end);
  zc::Vector<uint8_t> result(bytes.size());
  result.addAll(bytes.slice(0, first.begin));
  result.addAll(bytes.slice(second.begin, second.end));
  result.addAll(bytes.slice(first.begin, first.end));
  result.addAll(bytes.slice(second.end, bytes.size()));
  return result.releaseAsArray();
}

}  // namespace zomlang::compiler::tests::canonical_mutation
