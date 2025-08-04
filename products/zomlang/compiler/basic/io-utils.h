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

#pragma once

#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/io.h"
#include "zc/core/memory.h"

namespace zomlang {
namespace compiler {
namespace basic {

/// Synchronous file output stream for compiler output
///
/// This class provides a synchronous OutputStream implementation that writes
/// to a file using the zc::File interface. Unlike zc::async::FileOutputStream,
/// this implementation is designed for synchronous compiler operations.
class FileOutputStream : public zc::OutputStream {
public:
  explicit FileOutputStream(zc::Own<const zc::File> file);
  ~FileOutputStream() noexcept(false) override;

  void write(zc::ArrayPtr<const zc::byte> data) override;

  ZC_DISALLOW_COPY_AND_MOVE(FileOutputStream);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang