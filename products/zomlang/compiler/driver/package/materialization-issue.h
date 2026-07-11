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

#pragma once

#include <cstdint>

namespace zomlang::compiler::driver::package {

/// \brief Closed RFC 0012 source-materialization failure classification.
enum class MaterializationIssue : uint8_t {
  UnsupportedArchiveFormat = 0x01,
  ArchiveDecodeFailed = 0x02,
  TrailingArchiveData = 0x03,
  FreshDirectoryCreateFailed = 0x04,
  SourceReadFailed = 0x05,
  DestinationCreateFailed = 0x06,
  DestinationWriteFailed = 0x07,
  DestinationSyncFailed = 0x08,
  InvalidEntryEncoding = 0x09,
  AbsolutePath = 0x0a,
  ParentPath = 0x0b,
  DotPath = 0x0c,
  BackslashPath = 0x0d,
  EmptySegment = 0x0e,
  PathTooDeep = 0x0f,
  PathTooLong = 0x10,
  Symlink = 0x11,
  HardLink = 0x12,
  SpecialFile = 0x13,
  DuplicatePath = 0x14,
  UnicodeCollision = 0x15,
  CaseFoldCollision = 0x16,
  FileTooLarge = 0x17,
  CompressedSizeLimit = 0x18,
  DecoderWindowLimit = 0x19,
  DecoderMemoryLimit = 0x1a,
  ArchiveHeaderLimit = 0x1b,
  ArchiveMetadataLimit = 0x1c,
  FileCountLimit = 0x1d,
  TotalSizeLimit = 0x1e,
  LengthOverflow = 0x1f,
  SourceChangedDuringSnapshot = 0x20,
  SourceTreeDigestMismatch = 0x21,
  SnapshotCleanupFailed = 0x22
};

}  // namespace zomlang::compiler::driver::package
