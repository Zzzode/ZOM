#ifndef ZOM_BASIC_LANG_OPTIONS_H_
#define ZOM_BASIC_LANG_OPTIONS_H_

namespace zom {
namespace basic {

struct LangOptions {
  bool useUnicode;
  bool allowDollarIdentifiers;
  bool supportRegexLiterals;
  // more...

  LangOptions() : useUnicode(true), allowDollarIdentifiers(false), supportRegexLiterals(true) {}
};

}  // namespace basic
}  // namespace zom

#endif  // ZOM_BASIC_LANG_OPTIONS_H_
