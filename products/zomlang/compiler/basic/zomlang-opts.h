#ifndef ZOM_BASIC_LANG_OPTIONS_H_
#define ZOM_BASIC_LANG_OPTIONS_H_

namespace zomlang {
namespace compiler {

struct LangOptions {
  bool useUnicode;
  bool allowDollarIdentifiers;
  bool supportRegexLiterals;
  // more...

  LangOptions() : useUnicode(true), allowDollarIdentifiers(false), supportRegexLiterals(true) {}
};

}  // namespace compiler
}  // namespace zomlang

#endif  // ZOM_BASIC_LANG_OPTIONS_H_
