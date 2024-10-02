// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// Copyright (c) 2024 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#ifndef ZC_UTILITY_ONE_OF_
#define ZC_UTILITY_ONE_OF_

#include "src/zc/base/common.h"

namespace zc {
namespace _ {

template <uint32_t i, template <uint32_t> class Fail, typename Key,
          typename... Variants>
struct TypeIndex_;

template <uint32_t i, template <uint32_t> class Fail, typename Key,
          typename First, typename... Rest>
struct TypeIndex_<i, Fail, Key, First, Rest...> {
  static constexpr uint32_t value =
      TypeIndex_<i + 1, Fail, Key, Rest...>::value;
};

template <uint32_t i, template <uint32_t> class Fail, typename Key,
          typename... Rest>
struct TypeIndex_<i, Fail, Key, Key, Rest...> {
  static constexpr uint32_t value = i;
};

template <uint32_t i, template <uint32_t> class Fail, typename Key>
struct TypeIndex_<i, Fail, Key> : public Fail<i> {};

template <uint32_t i>
struct OneOfFailError_ {
  static_assert(i == -1, "type does not match any in OneOf");
};

template <uint32_t i>
struct OneOfFailZero_ {
  static constexpr int value = 0;
};

template <uint32_t i>
struct SuccessIfNotZero {
  using Success = int;
};

template <>
struct SuccessIfNotZero<0> {};

enum class Variants0 {};
enum class Variants1 { _variant0 };
enum class Variants2 { _variant0, _variant1 };
enum class Variants3 { _variant0, _variant1, _variant2 };
enum class Variants4 { _variant0, _variant1, _variant2, _variant3 };
enum class Variants5 { _variant0, _variant1, _variant2, _variant3, _variant4 };
enum class Variants6 {
  _variant0,
  _variant1,
  _variant2,
  _variant3,
  _variant4,
  _variant5
};
enum class Variants7 {
  _variant0,
  _variant1,
  _variant2,
  _variant3,
  _variant4,
  _variant5,
  _variant6
};
enum class Variants8 {
  _variant0,
  _variant1,
  _variant2,
  _variant3,
  _variant4,
  _variant5,
  _variant6,
  _variant7
};
enum class Variants9 {
  _variant0,
  _variant1,
  _variant2,
  _variant3,
  _variant4,
  _variant5,
  _variant6,
  _variant7,
  _variant8
};
enum class Variants10 {
  _variant0,
  _variant1,
  _variant2,
  _variant3,
  _variant4,
  _variant5,
  _variant6,
  _variant7,
  _variant8,
  _variant9
};

template <uint32_t i>
struct Variants_;
template <>
struct Variants_<0> {
  using Type = Variants0;
};
template <>
struct Variants_<1> {
  using Type = Variants1;
};
template <>
struct Variants_<2> {
  using Type = Variants2;
};
template <>
struct Variants_<3> {
  using Type = Variants3;
};
template <>
struct Variants_<4> {
  using Type = Variants4;
};
template <>
struct Variants_<5> {
  using Type = Variants5;
};
template <>
struct Variants_<6> {
  using Type = Variants6;
};
template <>
struct Variants_<7> {
  using Type = Variants7;
};
template <>
struct Variants_<8> {
  using Type = Variants8;
};
template <>
struct Variants_<9> {
  using Type = Variants9;
};
template <>
struct Variants_<10> {
  using Type = Variants10;
};

template <uint32_t i>
using Variants = typename Variants_<i>::Type;

}  // namespace _

template <typename... Variants>
class OneOf {
  // Get the 1-based index of Key within the type list Types, or static_assert
  // with a nice error.
  template <typename Key>
  static inline constexpr uint32_t typeIndex() {
    return _::TypeIndex_<1, _::OneOfFailError_, Key, Variants...>::value;
  }

  template <typename Key>
  static inline constexpr uint32_t typeIndexOrZero() {
    return _::TypeIndex_<1, _::OneOfFailZero_, Key, Variants...>::value;
  }

  template <uint32_t i, typename... OtherVariants>
  struct HasAll;
  // Has a member type called "Success" if and only if all of `OtherVariants`
  // are types that appear in `Variants`. Used with SFINAE to enable subset
  // constructors.

 public:
  inline OneOf() : tag_(0) {}

  OneOf(const OneOf &other) { copyFrom(other); }
  OneOf(OneOf &other) { copyFrom(other); }
  OneOf(OneOf &&other) { moveFrom(other); }

  // Copy/move from a value that matches one of the individual types in the
  // OneOf.
  template <typename T, typename = typename HasAll<0, Decay<T>>::Success>
  OneOf(T &&other) : tag_(typeIndex<Decay<T>>()) {
    ctor(*reinterpret_cast<Decay<T> *>(space), zc::fwd<T>(other));
  }

  ~OneOf() { destroy(); }

  OneOf &operator=(const OneOf &other) {
    if (tag_ != 0) destroy();
    copyFrom(other);
    return *this;
  }
  OneOf &operator=(OneOf &&other) noexcept {
    if (tag_ != 0) destroy();
    moveFrom(other);
    return *this;
  }

  inline bool operator==(std::nullptr_t) const { return tag_ == 0; }

  template <typename T>
  ZC_NODISCARD bool is() const {
    return tag_ == typeIndex<T>();
  }

  template <typename T>
  T &get() & {
    ZC_IREQUIRE(is<T>() && "Must check OneOf::is<T>() before calling get<T>()");
    return *reinterpret_cast<T *>(space);
  }

  template <typename T>
  T &&get() && {
    ZC_IREQUIRE(is<T>() && "Must check OneOf::is<T>() before calling get<T>()");
    return zc::mv(*reinterpret_cast<T *>(space));
  }

  template <typename T>
  const T &get() const & {
    ZC_IREQUIRE(is<T>() && "Must check OneOf::is<T>() before calling get<T>()");
    return *reinterpret_cast<const T *>(space);
  }

  template <typename T>
  const T &&get() const && {
    ZC_IREQUIRE(is<T>() && "Must check OneOf::is<T>() before calling get<T>()");
    return zc::mv(*reinterpret_cast<const T *>(space));
  }

  template <typename T, typename... Params>
  T &init(Params &&...params) {
    if (tag_ != 0) destroy();
    ctor(*reinterpret_cast<T *>(space), zc::fwd<Params>(params)...);
    tag_ = typeIndex<T>();
    return *reinterpret_cast<T *>(space);
  }

  template <typename T>
  Maybe<T &> tryGet() {
    if (is<T>()) {
      return *reinterpret_cast<T *>(space);
    } else {
      return zc::none;
    }
  }
  template <typename T>
  Maybe<const T &> tryGet() const {
    if (is<T>()) {
      return *reinterpret_cast<const T *>(space);
    } else {
      return zc::none;
    }
  }

  template <uint32_t i>
  ZC_NORETURN void allHandled();
  // After a series of if/else blocks handling each variant of the OneOf, have
  // the final else block call allHandled<n>() where n is the number of
  // variants. This will fail to compile if new variants are added in the
  // future.

  using Tag = _::Variants<sizeof...(Variants)>;

  Tag which() const {
    ZC_IREQUIRE(tag_ != 0, "Can't ZC_SWITCH_ONE_OF() on uninitialized value.");
    return static_cast<Tag>(tag_ - 1);
  }

  template <typename T>
  static constexpr Tag tagFor() {
    return static_cast<Tag>(typeIndex<T>() - 1);
  }

  OneOf *_switchSubject() & { return this; }
  const OneOf *_switchSubject() const & { return this; }
  _::NullableValue<OneOf> _switchSubject() && { return zc::mv(*this); }

 private:
  uint32_t tag_;

  static inline constexpr size_t maxSize(size_t a) { return a; }

  template <typename... Rest>
  static inline constexpr size_t maxSize(size_t a, size_t b, Rest... rest) {
    return maxSize(max(a, b), rest...);
  }

  static constexpr auto spaceSize = maxSize(sizeof(Variants)...);

  alignas(Variants...) unsigned char space[spaceSize];

  template <typename... T>
  inline void doAll(ZC_UNUSED T... t) {}

  template <typename T>
  inline bool destroyVariant() {
    if (tag_ == typeIndex<T>()) {
      tag_ = 0;
      dtor(*reinterpret_cast<T *>(space));
    }
    return false;
  }

  void destroy() { doAll(destroyVariant<Variants>()...); }

  template <typename T>
  inline bool copyVariantFrom(const OneOf &other) {
    if (other.is<T>()) {
      ctor(*reinterpret_cast<T *>(space), other.get<T>());
    }
    return false;
  }

  void copyFrom(const OneOf &other) {
    // Initialize as a copy of `other`. Expects that `this` starts out
    // uninitialized, so the tag is invalid.
    tag_ = other.tag_;
    doAll(copyVariantFrom<Variants>(other)...);
  }

  template <typename T>
  inline bool copyVariantFrom(OneOf &other) {
    if (other.is<T>()) {
      ctor(*reinterpret_cast<T *>(space), other.get<T>());
    }
    return false;
  }

  void copyFrom(OneOf &other) {
    // Initialize as a copy of `other`. Expects that `this` starts out
    // uninitialized, so the tag is invalid.
    tag_ = other.tag_;
    doAll(copyVariantFrom<Variants>(other)...);
  }

  template <typename T>
  inline bool moveVariantFrom(OneOf &other) {
    if (other.is<T>()) {
      ctor(*reinterpret_cast<T *>(space), zc::mv(other.get<T>()));
    }
    return false;
  }

  void moveFrom(OneOf &other) {
    // Initialize as a copy of `other`. Expects that `this` starts out
    // uninitialized, so the tag is invalid.
    tag_ = other.tag_;
    doAll(moveVariantFrom<Variants>(other)...);
  }
};

template <typename... Variants>
template <uint32_t i, typename First, typename... Rest>
struct OneOf<Variants...>::HasAll<i, First, Rest...>
    : public HasAll<typeIndexOrZero<First>(), Rest...> {};

template <typename... Variants>
template <uint32_t i>
struct OneOf<Variants...>::HasAll<i> : public _::SuccessIfNotZero<i> {};

template <typename... Variants>
template <uint32_t i>
void OneOf<Variants...>::allHandled() {
  // After a series of if/else blocks handling each variant of the OneOf, have
  // the final else block call allHandled<n>() where n is the number of
  // variants. This will fail to compile if new variants are added in the
  // future.

  static_assert(i == sizeof...(Variants),
                "new OneOf variants need to be handled here");
  ZC_UNREACHABLE;
}

#define ZC_SWITCH_ONE_OF(value)                               \
  switch (auto _zc_switch_subject = (value)._switchSubject(); \
          _zc_switch_subject->which())
#if !_MSC_VER || defined(__clang__)
#define ZC_CASE_ONE_OF(name, ...)                                      \
  break;                                                               \
  case ::zc::Decay<decltype(*_zc_switch_subject)>::template tagFor<    \
      __VA_ARGS__>():                                                  \
    for (auto &name = _zc_switch_subject->template get<__VA_ARGS__>(), \
              *_zc_switch_done = &name;                                \
         _zc_switch_done; _zc_switch_done = nullptr)
#else
// TODO(msvc): The latest MSVC which ships with VS2019 now ICEs on the
// implementation above. It appears we can hack around the problem by moving the
// `->template get<>()` syntax to an outer `if`. (This unfortunately allows
// wonky syntax like `ZC_CASE_ONE_OF(a, B) { } else { }`.)
// https://developercommunity.visualstudio.com/content/problem/1143733/internal-compiler-error-on-v1670.html
#define ZC_CASE_ONE_OF(name, ...)                                   \
  break;                                                            \
  case ::zc::Decay<decltype(*_zc_switch_subject)>::template tagFor< \
      __VA_ARGS__>():                                               \
    if (auto *_zc_switch_done =                                     \
            &_zc_switch_subject->template get<__VA_ARGS__>())       \
      for (auto &name = *_zc_switch_done; _zc_switch_done;          \
           _zc_switch_done = nullptr)
#endif
#define ZC_CASE_ONE_OF_DEFAULT \
  break;                       \
  default:

// Allows switching over a OneOf.
//
// Example:
//
//     zc::OneOf<int, float, const char*> variant;
//     ZC_SWITCH_ONE_OF(variant) {
//       ZC_CASE_ONE_OF(i, int) {
//         doSomethingWithInt(i);
//       }
//       ZC_CASE_ONE_OF(s, const char*) {
//         doSomethingWithString(s);
//       }
//       ZC_CASE_ONE_OF_DEFAULT {
//         doSomethingElse();
//       }
//     }
//
// Notes:
// - If you don't handle all possible types and don't include a default branch,
//   you'll get a compiler warning, just like a regular switch() over an enum
//   where one of the enum values is missing.
// - There's no need for a `break` statement in a ZC_CASE_ONE_OF; it is implied.
//
// Implementation notes:
// - The use of __VA_ARGS__ is to account for template types that have commas
//   separating type parameters, since macros don't recognize <> as grouping.
// - _zc_switch_done is really used as a boolean flag to prevent the for() loop
//   from actually looping, but it's defined as a pointer since that's all we
//   can define in this context.

}  // namespace zc

#endif  // ZC_UTILITY_ONE_OF_
