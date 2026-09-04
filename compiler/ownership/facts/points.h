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

#include "compiler/ownership/overlay/ownership-event-overlay.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Closed kind algebra for one ownership-analysis state point.
enum class PointKind : uint8_t { Cfg = 0x01, BeforeEvent = 0x02, AfterEvent = 0x03 };

/// \brief One CFG state point.
struct CfgPoint final {
  MirPoint point;
};

/// \brief State immediately before one ownership event.
struct BeforeEventPoint final {
  MirEventKey event;
};

/// \brief State immediately after one ownership event.
struct AfterEventPoint final {
  MirEventKey event;
};

/// \brief Exact RFC 0007 ownership point for analysis inputs and point states.
class Point final {
public:
  Point(Point&&) noexcept = default;
  Point& operator=(Point&&) noexcept = default;
  Point(const Point&) = default;
  Point& operator=(const Point&) = default;

  ZC_NODISCARD static Point cfg(MirPoint point) noexcept { return Point(CfgPoint{zc::mv(point)}); }
  ZC_NODISCARD static Point beforeEvent(MirEventKey event) noexcept {
    return Point(BeforeEventPoint{zc::mv(event)});
  }
  ZC_NODISCARD static Point afterEvent(MirEventKey event) noexcept {
    return Point(AfterEventPoint{zc::mv(event)});
  }
  ZC_NODISCARD PointKind kind() const noexcept {
    if (value.is<CfgPoint>()) return PointKind::Cfg;
    if (value.is<BeforeEventPoint>()) return PointKind::BeforeEvent;
    return PointKind::AfterEvent;
  }
  ZC_NODISCARD const CfgPoint& cfgValue() const { return value.get<CfgPoint>(); }
  ZC_NODISCARD const BeforeEventPoint& beforeEventValue() const {
    return value.get<BeforeEventPoint>();
  }
  ZC_NODISCARD const AfterEventPoint& afterEventValue() const {
    return value.get<AfterEventPoint>();
  }
  bool operator==(const Point& other) const noexcept {
    if (kind() != other.kind()) return false;
    switch (kind()) {
      case PointKind::Cfg:
        return cfgValue().point == other.cfgValue().point;
      case PointKind::BeforeEvent:
        return beforeEventValue().event == other.beforeEventValue().event;
      case PointKind::AfterEvent:
        return afterEventValue().event == other.afterEventValue().event;
    }
    return false;
  }
  bool operator!=(const Point& other) const noexcept { return !(*this == other); }

private:
  explicit Point(CfgPoint point) noexcept : value(zc::mv(point)) {}
  explicit Point(BeforeEventPoint point) noexcept : value(zc::mv(point)) {}
  explicit Point(AfterEventPoint point) noexcept : value(zc::mv(point)) {}
  zc::OneOf<CfgPoint, BeforeEventPoint, AfterEventPoint> value;
};

}  // namespace zomlang::compiler::ownership::facts
