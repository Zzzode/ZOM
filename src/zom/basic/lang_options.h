#ifndef ZOM_BASIC_LANG_OPTIONS_H_
#define ZOM_BASIC_LANG_OPTIONS_H_

namespace zom {
namespace basic {

struct LangOptions {
  bool use_unicode;
  bool allow_dollar_identifiers;
  bool support_regex_literals;
  // more...

  LangOptions()
      : use_unicode(true),
        allow_dollar_identifiers(false),
        support_regex_literals(true) {}
};

}  // namespace basic
}  // namespace zom

#endif  // ZOM_BASIC_LANG_OPTIONS_H_