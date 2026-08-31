// Copyright (c) 2026 Zode.Z. All rights reserved.
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

#include "runtime/drop.h"

extern "C" {

void __zom_drop(void* value) noexcept {
  // A builtin (trivial) drop has no runtime effect; the glue exists so the
  // ownership rail has a stable, linkable call target. Ignore the receiver.
  (void)value;
}
}
