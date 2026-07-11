# Unicode Character Database

ZOM's generated NFC normalization tables and conformance oracle are derived
from Unicode Character Database version 15.1.0.

## Inputs

| File | Canonical source | SHA-256 |
|---|---|---|
| `UnicodeData.txt` | <https://www.unicode.org/Public/15.1.0/ucd/UnicodeData.txt> | `2fc713e6a31a87c4850a37fe2caffa4218180fadb5de86b43a143ddb4581fb86` |
| `DerivedNormalizationProps.txt` | <https://www.unicode.org/Public/15.1.0/ucd/DerivedNormalizationProps.txt> | `06c4c289c006375125e92f7b7d791fa2327af940340556ebb0a8c3333818a548` |
| `NormalizationTest.txt` | <https://www.unicode.org/Public/15.1.0/ucd/NormalizationTest.txt> | `df5f516a85c1dc3ca8fc499d3ea4c7abee32a33c1cc561ec3a52549c57fd7051` |

## Generation

`scripts/codegen/gen_unicode_normalization.py` generates the compiler tables.
`scripts/codegen/gen_unicode_normalization_tests.py` generates the compact
normalization conformance oracle.

The Unicode data is distributed under the terms in `LICENSE.txt`.
