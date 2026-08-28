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

#include "compiler/format/doc.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::format {

// The document payload. Only the fields relevant to `kind` are populated:
//   Text            -> textValue
//   Concat, Fill    -> childList
//   Group, Indent   -> single (one child)
//   IfBreak         -> broken + flat
//   Line/Softline/Hardline -> none
struct Doc::Impl final {
  explicit Impl(DocKind kind) noexcept : kind(kind) {}

  DocKind kind;
  zc::String textValue;
  zc::Vector<Doc> childList;
  zc::Maybe<Doc> single;
  zc::Maybe<Doc> broken;
  zc::Maybe<Doc> flat;
};

Doc::Doc(zc::Own<Impl> impl) noexcept : impl(zc::mv(impl)) {}
Doc::Doc(Doc&&) noexcept = default;
Doc& Doc::operator=(Doc&&) noexcept = default;
Doc::~Doc() noexcept = default;

Doc Doc::text(zc::StringPtr bytes) {
  auto impl = zc::heap<Impl>(DocKind::Text);
  impl->textValue = zc::str(bytes);
  return Doc(zc::mv(impl));
}

Doc Doc::concat(zc::Array<Doc>&& docs) {
  auto impl = zc::heap<Impl>(DocKind::Concat);
  for (auto& doc : docs) { impl->childList.add(zc::mv(doc)); }
  return Doc(zc::mv(impl));
}

Doc Doc::line() { return Doc(zc::heap<Impl>(DocKind::Line)); }
Doc Doc::softline() { return Doc(zc::heap<Impl>(DocKind::Softline)); }
Doc Doc::hardline() { return Doc(zc::heap<Impl>(DocKind::Hardline)); }

Doc Doc::group(Doc&& doc) {
  auto impl = zc::heap<Impl>(DocKind::Group);
  impl->single = zc::mv(doc);
  return Doc(zc::mv(impl));
}

Doc Doc::indent(Doc&& doc) {
  auto impl = zc::heap<Impl>(DocKind::Indent);
  impl->single = zc::mv(doc);
  return Doc(zc::mv(impl));
}

Doc Doc::ifBreak(Doc&& broken, Doc&& flat) {
  auto impl = zc::heap<Impl>(DocKind::IfBreak);
  impl->broken = zc::mv(broken);
  impl->flat = zc::mv(flat);
  return Doc(zc::mv(impl));
}

Doc Doc::fill(zc::Array<Doc>&& docs) {
  auto impl = zc::heap<Impl>(DocKind::Fill);
  for (auto& doc : docs) { impl->childList.add(zc::mv(doc)); }
  return Doc(zc::mv(impl));
}

DocKind Doc::kind() const noexcept { return impl->kind; }

zc::StringPtr Doc::textValue() const {
  ZC_IREQUIRE(impl->kind == DocKind::Text, "textValue requires a Text document");
  return impl->textValue;
}

zc::ArrayPtr<const Doc> Doc::children() const {
  ZC_IREQUIRE(impl->kind == DocKind::Concat || impl->kind == DocKind::Fill,
              "children requires a Concat or Fill document");
  return impl->childList.asPtr();
}

const Doc& Doc::child() const {
  ZC_IREQUIRE(impl->kind == DocKind::Group || impl->kind == DocKind::Indent,
              "child requires a Group or Indent document");
  return ZC_REQUIRE_NONNULL(impl->single);
}

const Doc& Doc::ifBreakBroken() const {
  ZC_IREQUIRE(impl->kind == DocKind::IfBreak, "ifBreakBroken requires an IfBreak document");
  return ZC_REQUIRE_NONNULL(impl->broken);
}

const Doc& Doc::ifBreakFlat() const {
  ZC_IREQUIRE(impl->kind == DocKind::IfBreak, "ifBreakFlat requires an IfBreak document");
  return ZC_REQUIRE_NONNULL(impl->flat);
}

}  // namespace zomlang::compiler::format
