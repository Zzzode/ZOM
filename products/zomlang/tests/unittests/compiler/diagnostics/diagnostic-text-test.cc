// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/diagnostic-text.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::diagnostics {

ZC_TEST("DiagnosticTextTest.EscapesQuotesControlsBidiAndInvalidBytes") {
  const zc::byte bytes[] = {'`', '\'', '"', '\\', '\n', 0xe2, 0x80, 0xae, 0xff};
  ZC_EXPECT(escapeDiagnosticText(bytes, DiagnosticQuote::Backtick) ==
            "\\`'\"\\\\\\u{A}\\u{202E}\\xFF"_zc);
  ZC_EXPECT(escapeDiagnosticText(bytes, DiagnosticQuote::Single) ==
            "`\\'\"\\\\\\u{A}\\u{202E}\\xFF"_zc);
  ZC_EXPECT(escapeDiagnosticText(bytes, DiagnosticQuote::Double) ==
            "`'\\\"\\\\\\u{A}\\u{202E}\\xFF"_zc);
}

ZC_TEST("DiagnosticTextTest.TruncatesAtDecodedScalarBoundaries") {
  const zc::byte bytes[] = {'a', 0xc3, 0xa9, 'b'};
  ZC_EXPECT(escapeDiagnosticText(bytes, DiagnosticQuote::None, 2) == "a\\u{E9}..."_zc);
  ZC_EXPECT(escapeDiagnosticText(bytes, DiagnosticQuote::None, 3) == "a\\u{E9}b"_zc);
}

ZC_TEST("DiagnosticTextTest.RejectsOverlongAndSurrogateUtf8") {
  const zc::byte bytes[] = {0xc0, 0x80, 0xed, 0xa0, 0x80};
  ZC_EXPECT(escapeDiagnosticText(bytes) == "\\xC0\\x80\\xED\\xA0\\x80"_zc);
}

}  // namespace zomlang::compiler::diagnostics
