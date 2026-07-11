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

#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::identity {
namespace {

constexpr uint32_t kInitialState[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

constexpr uint32_t kRoundConstants[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t rotateRight(uint32_t value, uint32_t count) noexcept {
  return (value >> count) | (value << (32 - count));
}

uint32_t readUint32(zc::ArrayPtr<const uint8_t> bytes) {
  return (uint32_t{bytes[0]} << 24) | (uint32_t{bytes[1]} << 16) | (uint32_t{bytes[2]} << 8) |
         uint32_t{bytes[3]};
}

void writeUint32(zc::ArrayPtr<uint8_t> output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value >> 24);
  output[1] = static_cast<uint8_t>(value >> 16);
  output[2] = static_cast<uint8_t>(value >> 8);
  output[3] = static_cast<uint8_t>(value);
}

void compress(zc::ArrayPtr<uint32_t> state, zc::ArrayPtr<const uint8_t> block) {
  uint32_t schedule[64];
  for (uint32_t index = 0; index < 16; ++index) {
    schedule[index] = readUint32(block.slice(index * 4, index * 4 + 4));
  }
  for (uint32_t index = 16; index < 64; ++index) {
    const uint32_t previous15 = schedule[index - 15];
    const uint32_t previous2 = schedule[index - 2];
    const uint32_t sigma0 =
        rotateRight(previous15, 7) ^ rotateRight(previous15, 18) ^ (previous15 >> 3);
    const uint32_t sigma1 =
        rotateRight(previous2, 17) ^ rotateRight(previous2, 19) ^ (previous2 >> 10);
    schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

  for (uint32_t index = 0; index < 64; ++index) {
    const uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t temporary1 = h + sum1 + choice + kRoundConstants[index] + schedule[index];
    const uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = sum0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

}  // namespace

zc::Maybe<Sha256Digest> Sha256Digest::fromBytes(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() != 32) { return zc::none; }

  Sha256Digest result;
  for (size_t index = 0; index < bytes.size(); ++index) { result.value[index] = bytes[index]; }
  return result;
}

zc::Maybe<Sha256Digest> sha256(zc::ArrayPtr<const uint8_t> input) {
  if (input.size() > (~uint64_t{0}) / 8) { return zc::none; }

  uint32_t state[8];
  for (size_t index = 0; index < 8; ++index) { state[index] = kInitialState[index]; }

  size_t offset = 0;
  while (input.size() - offset >= 64) {
    compress(zc::arrayPtr(state), input.slice(offset, offset + 64));
    offset += 64;
  }

  uint8_t tail[128] = {};
  const size_t remainder = input.size() - offset;
  for (size_t index = 0; index < remainder; ++index) { tail[index] = input[offset + index]; }
  tail[remainder] = 0x80;

  const size_t tailSize = remainder < 56 ? 64 : 128;
  uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8;
  for (size_t index = 0; index < 8; ++index) {
    tail[tailSize - 1 - index] = static_cast<uint8_t>(bitLength);
    bitLength >>= 8;
  }
  compress(zc::arrayPtr(state), zc::arrayPtr(tail, tailSize).first(64));
  if (tailSize == 128) {
    compress(zc::arrayPtr(state), zc::arrayPtr(tail, tailSize).slice(64, 128));
  }

  Sha256Digest result;
  for (size_t index = 0; index < 8; ++index) {
    writeUint32(zc::arrayPtr(result.value).slice(index * 4, index * 4 + 4), state[index]);
  }
  return result;
}

}  // namespace zomlang::compiler::identity
