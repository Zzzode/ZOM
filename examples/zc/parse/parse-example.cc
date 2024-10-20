#include "src/zc/base/common.h"
#include "src/zc/base/debug.h"
#include "src/zc/base/function.h"
#include "src/zc/base/main.h"
#include "src/zc/memory/arena.h"
#include "src/zc/parse/char.h"
#include "src/zc/parse/common.h"
#include "src/zc/strings/string.h"
#include "src/zc/utility/time.h"

namespace examples {

namespace p = zc::parse;

// <expression> ::= <term> { <addop> <term> }
// <term> ::= <factor> { <mulop> <factor> }
// <factor> ::= <number> | "(" <expression> ")"
// <addop> ::= "+" | "-"
// <mulop> ::= "*" | "/"
// <number> ::= <digit>+ [ "." <digit>* ]
// <digit> ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"

class ExpressionParser {
  using ParserInput = p::IteratorInput<char, const char*>;

public:
  ExpressionParser() {
    auto& expression = expression_;
    auto& factor = arena_.copy(p::oneOf(
        p::number, p::transform(p::sequence(p::exactChar<'('>(), expression, p::exactChar<')'>()),
                                [](double f) { return f; })));
    auto& addop =
        arena_.copy(p::oneOf(constResult(p::exactly('+'), '+'), constResult(p::exactly('-'), '-')));
    auto& mulop =
        arena_.copy(p::oneOf(constResult(p::exactly('*'), '*'), constResult(p::exactly('/'), '/')));
    auto& term =
        arena_.copy(p::transform(p::sequence(factor, p::many(p::sequence(mulop, factor))),
                                 [](double f, const zc::Array<zc::Tuple<char, double>>& res) {
                                   for (auto& m : res) {
                                     const char op = zc::get<0>(m);
                                     if (op == '*') {
                                       f *= zc::get<1>(m);
                                     } else {
                                       f /= zc::get<1>(m);
                                     }
                                   }
                                   return f;
                                 }));
    expression_ = arena_.copy(
        p::transform(p::sequence(term, p::many(p::sequence(addop, term))),
                     [](double f, const zc::Array<zc::Tuple<char, double>>& res) -> double {
                       for (auto& m : res) {
                         const char op = zc::get<0>(m);
                         if (op == '+') {
                           f += zc::get<1>(m);
                         } else {
                           f -= zc::get<1>(m);
                         }
                       }
                       return f;
                     }));
  }

  ZC_NODISCARD ZC_ALWAYS_INLINE zc::Maybe<double> parse(const zc::StringPtr input) const {
    ParserInput parserInput(input.begin(), input.end());
    return expression_(parserInput);
  }

  ZC_NODISCARD zc::Duration measureParseTime(const zc::StringPtr input,
                                             const int iterations = 1000) const {
    zc::Duration totalTime = 0 * zc::NANOSECONDS;
    for (int i = 0; i < iterations; ++i) {
      zc::TimePoint start = zc::systemPreciseMonotonicClock().now();
      auto result = parse(input);
      zc::TimePoint end = zc::systemPreciseMonotonicClock().now();
      totalTime += end - start;
    }
    return totalTime / iterations;
  }

private:
  zc::Arena arena_;
  p::ParserRef<ParserInput, double> expression_;
};

}  // namespace examples

class MainClass {
public:
  explicit MainClass(zc::ProcessContext& context) : context_(context) {}

  zc::MainBuilder::Validity SetExpression(zc::StringPtr expr) {
    expression_ = expr;
    return true;
  }

  zc::MainFunc getMain() {
    return zc::MainBuilder(context_, "Expression Calculator v1.0",
                           "Calculates the result of an addition/subtraction expression.")
        .expectOneOrMoreArgs("<expression>", ZC_BIND_METHOD(*this, SetExpression))
        .addOption(
            {'d', "detail"},
            [this]() {
              verbose_ = true;
              return true;
            },
            "Enable detailed output.")
        .callAfterParsing(ZC_BIND_METHOD(*this, Calculate))
        .build();
  }

private:
  zc::MainBuilder::Validity Calculate() {
    if (expression_ == nullptr) { return "No expression provided."; }

    const examples::ExpressionParser parser;
    ZC_IF_SOME(result, parser.parse(expression_)) {
      zc::Duration averageTime = parser.measureParseTime(expression_);

      if (verbose_) {
        context_.exitInfo(zc::str("Expression: ", expression_, "\nResult: ", result,
                                  "\nAverage parsing time: ", averageTime));
      }
      context_.exitInfo(zc::str(result, "\nAverage parsing time: ", averageTime));
    }

    return "Failed to parse the expression.";
  }

  zc::ProcessContext& context_;
  zc::StringPtr expression_;
  bool verbose_ = false;
};

ZC_MAIN(MainClass)
