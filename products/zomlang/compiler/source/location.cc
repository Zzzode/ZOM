// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/source/location.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace source {

ZC_NODISCARD zc::String SourceLoc::toString(const SourceManager& sm, uint64_t& lastBufferId) const {
  if (isInvalid()) { return zc::str("SourceLoc(invalid)"); }

  auto bufferId = ZC_ASSERT_NONNULL(sm.findBufferContainingLoc(*this));

  zc::StringPtr prefix;
  if (bufferId != lastBufferId) {
    prefix = sm.getIdentifierForBuffer(bufferId);
    lastBufferId = bufferId;
  } else {
    prefix = "line";
  }

  auto lineAndCol = sm.getPresumedLineAndColumnForLoc(*this, bufferId);

  return zc::str("SourceLoc(", prefix, ":", lineAndCol.line, ":", lineAndCol.column, " @ 0x",
                 zc::hex(reinterpret_cast<uintptr_t>(ptr)), ")");
}

void SourceLoc::print(zc::OutputStream& os, const SourceManager& sm) const {
  uint64_t tmp = ~0ULL;
  os.write(toString(sm, tmp).asBytes());
}

zc::String SourceRange::getText(const SourceManager& sm) const {
  if (isInvalid()) { return zc::str(""); }
  zc::ArrayPtr<const zc::byte> textBytes = sm.extractText(*this, zc::none);
  return zc::str(textBytes.asChars());
}

zc::String CharSourceRange::getText(SourceManager& sm) const {
  if (start.isInvalid() || end.isInvalid()) { return zc::str(""); }
  SourceRange range(start, end);
  auto textBytes = sm.extractText(range, zc::none);
  return zc::str(textBytes.asChars());
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
