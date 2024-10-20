#ifndef ZOM_LEXER_TOKEN_H_
#define ZOM_LEXER_TOKEN_H_

#include "src/zom/source/location.h"

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
  explicit Token(const TokenDesc& desc) : desc_(desc) {}

  ZC_NODISCARD tok kind() const { return desc_.kind; }
  ZC_NODISCARD const char* start() const { return desc_.start; }
  ZC_NODISCARD unsigned length() const { return desc_.length; }
  ZC_NODISCARD source::SourceLoc location() const { return desc_.loc; }

private:
  TokenDesc desc_;
};

}  // namespace lexer
}  // namespace zom

#endif  // ZOM_LEXER_TOKEN_H_