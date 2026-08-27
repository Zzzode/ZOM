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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Closed kind algebra for one ownership-analysis state point.
enum class OwnershipPointKind : uint8_t { Cfg = 0x01, BeforeEvent = 0x02, AfterEvent = 0x03 };

/// \brief One CFG state point.
struct OwnershipCfgPoint final {
  MirPoint point;
};

/// \brief State immediately before one ownership event.
struct OwnershipBeforeEventPoint final {
  MirEventKey event;
};

/// \brief State immediately after one ownership event.
struct OwnershipAfterEventPoint final {
  MirEventKey event;
};

/// \brief Exact RFC 0007 ownership point for analysis inputs and point states.
class OwnershipPoint final {
public:
  OwnershipPoint(OwnershipPoint&&) noexcept = default;
  OwnershipPoint& operator=(OwnershipPoint&&) noexcept = default;
  OwnershipPoint(const OwnershipPoint&) = default;
  OwnershipPoint& operator=(const OwnershipPoint&) = default;

  ZC_NODISCARD static OwnershipPoint cfg(MirPoint point) noexcept {
    return OwnershipPoint(OwnershipCfgPoint{zc::mv(point)});
  }
  ZC_NODISCARD static OwnershipPoint beforeEvent(MirEventKey event) noexcept {
    return OwnershipPoint(OwnershipBeforeEventPoint{zc::mv(event)});
  }
  ZC_NODISCARD static OwnershipPoint afterEvent(MirEventKey event) noexcept {
    return OwnershipPoint(OwnershipAfterEventPoint{zc::mv(event)});
  }
  ZC_NODISCARD OwnershipPointKind kind() const noexcept {
    if (value.is<OwnershipCfgPoint>()) return OwnershipPointKind::Cfg;
    if (value.is<OwnershipBeforeEventPoint>()) return OwnershipPointKind::BeforeEvent;
    return OwnershipPointKind::AfterEvent;
  }
  ZC_NODISCARD const OwnershipCfgPoint& cfgValue() const { return value.get<OwnershipCfgPoint>(); }
  ZC_NODISCARD const OwnershipBeforeEventPoint& beforeEventValue() const {
    return value.get<OwnershipBeforeEventPoint>();
  }
  ZC_NODISCARD const OwnershipAfterEventPoint& afterEventValue() const {
    return value.get<OwnershipAfterEventPoint>();
  }
  bool operator==(const OwnershipPoint& other) const noexcept {
    if (kind() != other.kind()) return false;
    switch (kind()) {
      case OwnershipPointKind::Cfg:
        return cfgValue().point == other.cfgValue().point;
      case OwnershipPointKind::BeforeEvent:
        return beforeEventValue().event == other.beforeEventValue().event;
      case OwnershipPointKind::AfterEvent:
        return afterEventValue().event == other.afterEventValue().event;
    }
    return false;
  }
  bool operator!=(const OwnershipPoint& other) const noexcept { return !(*this == other); }

private:
  explicit OwnershipPoint(OwnershipCfgPoint point) noexcept : value(zc::mv(point)) {}
  explicit OwnershipPoint(OwnershipBeforeEventPoint point) noexcept : value(zc::mv(point)) {}
  explicit OwnershipPoint(OwnershipAfterEventPoint point) noexcept : value(zc::mv(point)) {}
  zc::OneOf<OwnershipCfgPoint, OwnershipBeforeEventPoint, OwnershipAfterEventPoint> value;
};

}  // namespace zomlang::compiler::ownership::facts
