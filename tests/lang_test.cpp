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
  node_ptr root;
  if (true) {
    // Degenerate case.
    root = M<always_true>();
    EXPECT_EQ((root->print()), "true");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // Always true.
    root = M<and_junction>(M<always_true>(),
        M<or_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "and:(true, or:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // Always false.
    root = M<and_junction>(M<always_false>(),
        M<or_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "and:(false, or:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "false");
  }
  if (true) {
    // One always-true and a collapsed AND.
    root = M<and_junction>(M<exists>("A"s),
        M<or_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "and:(exists:(A), or:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(A)");
  }
  if (true) {
    // One always-false and a collapsed OR.
    root = M<or_junction>(M<exists>("A"s),
        M<and_junction>(M<always_false>(), M<always_true>()));
    EXPECT_EQ((root->print()), "or:(exists:(A), and:(false, true))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(A)");
  }
  if (true) {
    // Negations of terminals.
    node_ptr root;

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

    // Negating an EQ should yield an NE.
    root = M<not_junction>(M<eq>("abc"s, 42));
    EXPECT_EQ((root->print()), "not:(eq:(abc, 42))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "ne:(abc, 42)");

    // Negating an NE should yield an EQ.
    root = M<not_junction>(M<ne>("abc"s, 42));
    EXPECT_EQ((root->print()), "not:(ne:(abc, 42))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "eq:(abc, 42)");

    // Negating an EXISTS should yield an ABSENT.
    root = M<not_junction>(M<exists>("abc"s));
    EXPECT_EQ((root->print()), "not:(exists:(abc))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "absent:(abc)");

    // Negating an ABSENT should yield an EXISTS.
    root = M<not_junction>(M<absent>("abc"s));
    EXPECT_EQ((root->print()), "not:(absent:(abc))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(abc)");
  }
  if (true) {
    // Collapsed nested pairs of NOT.
    root = M<not_junction>(M<not_junction>(M<exists>("A"s)));
    EXPECT_EQ((root->print()), "not:(not:(exists:(A)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(A)");

    // Four negations should cancel out.
    root = M<not_junction>(
        M<not_junction>(M<not_junction>(M<not_junction>(M<exists>("A"s)))));

    EXPECT_EQ((root->print()), "not:(not:(not:(not:(exists:(A)))))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(A)");

    // Three negations should leave one (which reverses the terminal).
    root = M<not_junction>(M<not_junction>(M<not_junction>(M<exists>("A"s))));
    EXPECT_EQ((root->print()), "not:(not:(not:(exists:(A))))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "absent:(A)");
  }
  if (true) {
    // Flatten nested ORs.
    root = M<or_junction>(M<exists>("A"s),
        M<or_junction>(M<exists>("B"s), M<exists>("C"s)));
    EXPECT_EQ((root->print()), "or:(exists:(A), or:(exists:(B), exists:(C)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "or:(exists:(A), exists:(B), exists:(C))");
  }
  if (true) {
    // Flatten nested ANDs.
    root = M<and_junction>(M<exists>("A"s),
        M<and_junction>(M<exists>("B"s), M<exists>("C"s)));
    EXPECT_EQ((root->print()),
        "and:(exists:(A), and:(exists:(B), exists:(C)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "and:(exists:(A), exists:(B), exists:(C))");
  }
  if (true) {
    // AND without nodes is always true.
    root = M<and_junction>();
    EXPECT_EQ((root->print()), "and:()");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // OR without nodes is always true.
    root = M<or_junction>();
    EXPECT_EQ((root->print()), "or:()");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "true");
  }
  if (true) {
    // AND with one node is that node.
    root = M<and_junction>(M<exists>("A"s));
    EXPECT_EQ((root->print()), "and:(exists:(A))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(A)");
  }
  if (true) {
    // OR with one node is that node.
    root = M<or_junction>(M<exists>("A"s));
    EXPECT_EQ((root->print()), "or:(exists:(A))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()), "exists:(A)");
  }
  if (true) {
    // Distribute OR over AND: A AND(B OR C) = (A AND B)OR(A AND C)
    root = M<and_junction>(M<exists>("A"s),
        M<or_junction>(M<exists>("B"s), M<exists>("C"s)));
    EXPECT_EQ((root->print()),
        "and:(exists:(A), or:(exists:(B), exists:(C)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(and:(exists:(A), exists:(B)), and:(exists:(A), exists:(C)))");
  }
  if (true) {
    // Do not distribute AND over OR: A OR (B AND C) = A OR (B AND C)
    root = M<or_junction>(M<exists>("A"s),
        M<and_junction>(M<exists>("B"s), M<exists>("C"s)));
    EXPECT_EQ((root->print()),
        "or:(exists:(A), and:(exists:(B), exists:(C)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(exists:(A), and:(exists:(B), exists:(C)))");
  }
  if (true) {
    // Distribute OR over AND: A AND B AND(C OR D) = (A AND B AND C)OR(A AND B
    // AND C)
    root = M<and_junction>(M<exists>("A"s), M<exists>("B"s),
        M<or_junction>(M<exists>("C"s), M<exists>("D"s)));
    EXPECT_EQ((root->print()),
        "and:(exists:(A), exists:(B), or:(exists:(C), exists:(D)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(and:(exists:(A), exists:(B), exists:(C)), and:(exists:(A), "
        "exists:(B), exists:(D)))");
  }
  if (true) {
    // Do not distribute ANDs over OR: (A AND B) OR (C AND D) =
    // (A AND B) OR (C AND D)
    root = M<or_junction>(M<and_junction>(M<exists>("A"s), M<exists>("B"s)),
        M<and_junction>(M<exists>("C"s), M<exists>("D"s)));
    EXPECT_EQ((root->print()),
        "or:(and:(exists:(A), exists:(B)), and:(exists:(C), exists:(D)))");
    root = dnf::convert(root);
    EXPECT_EQ((root->print()),
        "or:(and:(exists:(A), exists:(B)), and:(exists:(C), exists:(D)))");
  }
  if (true) {
    // Distribute OR over AND: (A OR B) AND (C OR D) = (A AND C) OR (A AND D)
    // OR (B AND C) OR (B AND D)
    root = M<and_junction>(M<or_junction>(M<exists>("A"s), M<exists>("B"s)),
        M<or_junction>(M<exists>("C"s), M<exists>("D"s)));
    EXPECT_EQ((root->print()),
        "and:(or:(exists:(A), exists:(B)), or:(exists:(C), exists:(D)))");
    root = dnf::convert(root);
    // TODO: This is completely wrong.
    EXPECT_EQ((root->print()),
        "or:(and:(or:(exists:(C), exists:(D)), exists:(A)), "
        "and:(or:(exists:(C), exists:(D)), exists:(B)), and:(or:(exists:(A), "
        "exists:(B)), exists:(C)), and:(or:(exists:(A), exists:(B)), "
        "exists:(D)))");
  }
}

MAKE_TEST_LIST(LangTest_AstPred);