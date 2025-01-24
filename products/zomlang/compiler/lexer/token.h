#ifndef ZOM_LEXER_TOKEN_H_
#define ZOM_LEXER_TOKEN_H_

#include "zomlang/compiler/source/location.h"

namespace zom {
namespace lexer {

enum class tok {
  kUnknown,
  kIdentifier,
  kKeyword,
  kInteger,
  kFloat,
  kString,
  kOperator,
  kPunctuation,
  kComment,
  kEOF,
  // Add more token types as needed...
};

struct TokenDesc {
  tok kind;
  const char* start;
  unsigned length;
  source::SourceLoc loc;

  TokenDesc() : kind(tok::kUnknown), start(nullptr), length(0) {}
  TokenDesc(const tok k, const char* s, const unsigned len, const source::SourceLoc l)
      : kind(k), start(s), length(len), loc(l) {}
};

class Token {
public:
  Token() = default;
  explicit Token(const TokenDesc& desc) : desc(desc) {}

  ZC_NODISCARD tok getKind() const { return desc.kind; }
  ZC_NODISCARD const char* getStart() const { return desc.start; }
  ZC_NODISCARD unsigned getLength() const { return desc.length; }
  ZC_NODISCARD source::SourceLoc getLocation() const { return desc.loc; }

private:
  TokenDesc desc;
};

}  // namespace lexer
}  // namespace zom

#endif  // ZOM_LEXER_TOKEN_H_
