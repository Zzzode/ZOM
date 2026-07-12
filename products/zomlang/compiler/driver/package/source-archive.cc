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

#include "zomlang/compiler/driver/package/source-archive.h"

#include "zomlang/compiler/driver/package/archive-reader.h"

namespace zomlang::compiler::driver::package {

SourceArchiveAdmission::SourceArchiveAdmission(SourceAdmissionLimits limits) : limits(limits) {}

SourceTreeBuildResult SourceArchiveAdmission::admit(ZstdInput& input) {
  ZstdDecoder decoder(limits);
  auto decoded = decoder.openDecodedInput(input);
  if (decoded.is<MaterializationIssue>()) { return decoded.get<MaterializationIssue>(); }

  auto archiveInput = zc::mv(decoded.get<zc::Own<ArchiveInput>>());
  ArchiveReader reader(limits);
  SourceTreeBuilder builder;
  ZC_IF_SOME(issue, reader.read(*archiveInput, builder)) { return issue; }
  return builder.finish();
}

}  // namespace zomlang::compiler::driver::package
