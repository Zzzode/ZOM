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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::identity {

/// \brief Runtime identity authority paired with one arena-local typed handle.
template <typename Key, typename Record, typename Handle>
class MaterializedIdentityEntry final {
public:
  ~MaterializedIdentityEntry() noexcept(false) = default;
  MaterializedIdentityEntry(MaterializedIdentityEntry&&) noexcept = default;
  MaterializedIdentityEntry& operator=(MaterializedIdentityEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(MaterializedIdentityEntry);

  ZC_NODISCARD static MaterializedIdentityEntry fromVerified(Key&& key, Record&& record,
                                                             Handle handle) noexcept {
    return MaterializedIdentityEntry(zc::heap<Impl>(zc::mv(key), zc::mv(record), handle));
  }
  ZC_NODISCARD MaterializedIdentityEntry clone() const {
    return MaterializedIdentityEntry(
        zc::heap<Impl>(impl->key.clone(), impl->record.clone(), impl->handle));
  }
  ZC_NODISCARD const Key& key() const noexcept { return impl->key; }
  ZC_NODISCARD const Record& record() const noexcept { return impl->record; }
  ZC_NODISCARD Handle handle() const noexcept { return impl->handle; }

private:
  struct Impl final {
    Impl(Key&& key, Record&& record, Handle handle) noexcept
        : key(zc::mv(key)), record(zc::mv(record)), handle(handle) {}

    Key key;
    Record record;
    Handle handle;
  };

  explicit MaterializedIdentityEntry(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}

  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::identity
