// Copyright (c) 2021 Cloudflare, Inc. and contributors
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

#include "zc/core/list.h"

#include <zc/ztest/test.h>

namespace zc {
namespace {

struct TestElement {
  int i;
  ListLink<TestElement> link;

  TestElement(int i) : i(i) {}
};

ZC_TEST("List") {
  List<TestElement, &TestElement::link> list;
  ZC_EXPECT(list.empty());
  ZC_EXPECT(list.size() == 0);

  TestElement foo(123);
  TestElement bar(456);
  TestElement baz(789);

  {
    list.add(foo);
    ZC_DEFER(list.remove(foo));
    ZC_EXPECT(!list.empty());
    ZC_EXPECT(list.size() == 1);
    ZC_EXPECT(list.front().i == 123);

    {
      list.add(bar);
      ZC_EXPECT(list.size() == 2);
      ZC_DEFER(list.remove(bar));

      {
        auto iter = list.begin();
        ZC_ASSERT(iter != list.end());
        ZC_EXPECT(iter->i == 123);
        ++iter;
        ZC_ASSERT(iter != list.end());
        ZC_EXPECT(iter->i == 456);
        iter->i = 321;
        ZC_EXPECT(bar.i == 321);
        ++iter;
        ZC_ASSERT(iter == list.end());
      }

      const List<TestElement, &TestElement::link>& clist = list;

      {
        auto iter = clist.begin();
        ZC_ASSERT(iter != clist.end());
        ZC_EXPECT(iter->i == 123);
        ++iter;
        ZC_ASSERT(iter != clist.end());
        ZC_EXPECT(iter->i == 321);
        ++iter;
        ZC_ASSERT(iter == clist.end());
      }

      {
        list.addFront(baz);
        ZC_EXPECT(list.size() == 3);
        ZC_DEFER(list.remove(baz));

        {
          auto iter = list.begin();
          ZC_ASSERT(iter != list.end());
          ZC_EXPECT(iter->i == 789);
          ++iter;
          ZC_ASSERT(iter != list.end());
          ZC_EXPECT(iter->i == 123);
          ++iter;
          ZC_ASSERT(iter != list.end());
          ZC_EXPECT(iter->i == 321);
          ++iter;
          ZC_ASSERT(iter == list.end());
        }
      }
    }

    ZC_EXPECT(list.size() == 1);

    ZC_EXPECT(!list.empty());
    ZC_EXPECT(list.front().i == 123);

    {
      auto iter = list.begin();
      ZC_ASSERT(iter != list.end());
      ZC_EXPECT(iter->i == 123);
      ++iter;
      ZC_ASSERT(iter == list.end());
    }
  }

  ZC_EXPECT(list.empty());
  ZC_EXPECT(list.size() == 0);

  {
    list.addFront(bar);
    ZC_DEFER(list.remove(bar));
    ZC_EXPECT(!list.empty());
    ZC_EXPECT(list.size() == 1);
    ZC_EXPECT(list.front().i == 321);

    {
      auto iter = list.begin();
      ZC_ASSERT(iter != list.end());
      ZC_EXPECT(iter->i == 321);
      ++iter;
      ZC_ASSERT(iter == list.end());
    }

    {
      list.add(baz);
      ZC_EXPECT(list.size() == 2);
      ZC_DEFER(list.remove(baz));

      {
        auto iter = list.begin();
        ZC_ASSERT(iter != list.end());
        ZC_EXPECT(iter->i == 321);
        ++iter;
        ZC_ASSERT(iter != list.end());
        ZC_EXPECT(iter->i == 789);
        ++iter;
        ZC_ASSERT(iter == list.end());
      }
    }
  }

  ZC_EXPECT(list.empty());
  ZC_EXPECT(list.size() == 0);
}

ZC_TEST("List remove while iterating") {
  List<TestElement, &TestElement::link> list;
  ZC_EXPECT(list.empty());

  TestElement foo(123);
  list.add(foo);
  ZC_DEFER(list.remove(foo));

  TestElement bar(456);
  list.add(bar);

  TestElement baz(789);
  list.add(baz);
  ZC_DEFER(list.remove(baz));

  ZC_EXPECT(foo.link.isLinked());
  ZC_EXPECT(bar.link.isLinked());
  ZC_EXPECT(baz.link.isLinked());

  {
    auto iter = list.begin();
    ZC_ASSERT(iter != list.end());
    ZC_EXPECT(iter->i == 123);
    ++iter;

    ZC_ASSERT(iter != list.end());
    ZC_EXPECT(iter->i == 456);
    list.remove(*iter);
    ++iter;

    ZC_ASSERT(iter != list.end());
    ZC_EXPECT(iter->i == 789);
    ++iter;

    ZC_EXPECT(iter == list.end());
  }

  ZC_EXPECT(foo.link.isLinked());
  ZC_EXPECT(!bar.link.isLinked());
  ZC_EXPECT(baz.link.isLinked());

  {
    auto iter = list.begin();
    ZC_ASSERT(iter != list.end());
    ZC_EXPECT(iter->i == 123);
    ++iter;

    ZC_ASSERT(iter != list.end());
    ZC_EXPECT(iter->i == 789);
    ++iter;

    ZC_EXPECT(iter == list.end());
  }
}

}  // namespace
}  // namespace zc
