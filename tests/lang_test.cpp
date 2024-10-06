// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "../corvid/lang.h"
#include "AccutestShim.h"

using namespace std::literals;
using namespace corvid::lang::ast_pred;

template<operation op, typename... Args>
[[nodiscard]] node_ptr M(Args&&... args) {
  return make<op>(std::forward<Args>(args)...);
}

void LangTest_AstPred() {
  using enum operation;
  if (true) {
    // Degenerate case.
    auto root = M<always_true>();
    EXPECT_EQ((root->print()), "true");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // Always true.
    auto root = M<and_junction>(M<always_true>(),
        M<or_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "and:(true, or:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // Always false.
    auto root = M<and_junction>(M<always_false>(),
        M<or_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "and:(false, or:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "false");
  }
  if (true) {
    // One always-true and a collapsed AND.
    auto root = M<and_junction>(M<eq>("abc"s, 42),
        M<or_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "and:(eq:(abc, 42), or:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");
  }
  if (true) {
    // One always-false and a collapsed OR.
    auto root = M<or_junction>(M<eq>("abc"s, 42),
        M<and_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "or:(eq:(abc, 42), and:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");
  }
  if (true) {
    // Collapsed nested pairs of NOT.
    auto root = M<not_junction>(M<not_junction>(M<eq>("abc"s, 42)));
    EXPECT_EQ((root->print()), "not:(not:(eq:(abc, 42)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");

    // Four negations should cancel out.
    root = M<not_junction>(
        M<not_junction>(M<not_junction>(M<not_junction>(M<eq>("abc"s, 42)))));

    EXPECT_EQ((root->print()), "not:(not:(not:(not:(eq:(abc, 42)))))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");

    // Three negations should leave one.
    root =
        M<not_junction>(M<not_junction>(M<not_junction>(M<eq>("abc"s, 42))));
    EXPECT_EQ((root->print()), "not:(not:(not:(eq:(abc, 42))))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "not:(eq:(abc, 42))");

    // Negating a true should yield a false.
    root = M<not_junction>(M<always_true>());
    EXPECT_EQ((root->print()), "not:(true)");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "false");

    // Negating a false should yield a true.
    root = M<not_junction>(M<always_false>());
    EXPECT_EQ((root->print()), "not:(false)");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // Flatten nested ORs.
    auto root = M<or_junction>(M<eq>("abc"s, 42),
        M<or_junction>(M<eq>("def"s, 43), M<eq>("ghi"s, 44)));
    EXPECT_EQ((root->print()),
        "or:(eq:(abc, 42), or:(eq:(def, 43), eq:(ghi, 44)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(eq:(abc, 42), eq:(def, 43), eq:(ghi, 44))");
  }
  if (true) {
    // Flatten nested ANDs.
    auto root = M<and_junction>(M<eq>("abc"s, 42),
        M<and_junction>(M<eq>("def"s, 43), M<eq>("ghi"s, 44)));
    EXPECT_EQ((root->print()),
        "and:(eq:(abc, 42), and:(eq:(def, 43), eq:(ghi, 44)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "and:(eq:(abc, 42), eq:(def, 43), eq:(ghi, 44))");
  }
  if (true) {
    // AND without nodes is always true.
    auto root = M<and_junction>();
    EXPECT_EQ((root->print()), "and:()");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // OR without nodes is always true.
    auto root = M<or_junction>();
    EXPECT_EQ((root->print()), "or:()");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // AND with one node is that node.
    auto root = M<and_junction>(M<eq>("abc"s, 42));
    EXPECT_EQ((root->print()), "and:(eq:(abc, 42))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");
  }
  if (true) {
    // OR with one node is that node.
    auto root = M<or_junction>(M<eq>("abc"s, 42));
    EXPECT_EQ((root->print()), "or:(eq:(abc, 42))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");
  }
  if (true) {
    // Distribute OR over AND: A AND(B OR C) = (A AND B)OR(A AND C)
    auto root = M<and_junction>(M<eq>("abc"s, 42),
        M<or_junction>(M<eq>("def"s, 43), M<eq>("ghi"s, 44)));
    EXPECT_EQ((root->print()),
        "and:(eq:(abc, 42), or:(eq:(def, 43), eq:(ghi, 44)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(and:(eq:(abc, 42), eq:(def, 43)), and:(eq:(abc, 42), eq:(ghi, "
        "44)))");
  }
  if (true) {
    // Distribute AND over OR: A OR (B AND C) = (A OR B) AND (A OR C)
    auto root = M<or_junction>(M<eq>("abc"s, 42),
        M<and_junction>(M<eq>("def"s, 43), M<eq>("ghi"s, 44)));
    EXPECT_EQ((root->print()),
        "or:(eq:(abc, 42), and:(eq:(def, 43), eq:(ghi, 44)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "and:(or:(eq:(abc, 42), eq:(def, 43)), or:(eq:(abc, 42), eq:(ghi, "
        "44)))");
  }
  if (true) {
    // Distribute OR over AND: A AND B AND(C OR D) = (A AND B AND C)OR(A AND B
    // AND C)
    auto root = M<and_junction>(M<eq>("abc"s, 42), M<eq>("def"s, 43),
        M<or_junction>(M<eq>("ghi"s, 44), M<eq>("jkl"s, 45)));
    EXPECT_EQ((root->print()),
        "and:(eq:(abc, 42), eq:(def, 43), or:(eq:(ghi, 44), eq:(jkl, 45)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(and:(eq:(abc, 42), eq:(def, 43), eq:(ghi, 44)), and:(eq:(abc, "
        "42), eq:(def, 43), eq:(jkl, 45)))");
  }
}

MAKE_TEST_LIST(LangTest_AstPred);
