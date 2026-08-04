// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
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

#include <iostream>
#include <string>

#include "corvid/lang/coreb/coreb.h"

using namespace corvid;
using namespace corvid::coreb;

// Minimal interactive REPL for the CoreB kernel (milestone 2).
//
// Reads forms line by line, evaluating each and printing its value. A line
// whose read fails as incomplete (an unbalanced list or an open string)
// continues onto the next line instead of reporting an error. Exit with
// end-of-input: Ctrl+Z then Enter on Windows, Ctrl+D elsewhere.
int main() {
  runtime rt;
  evaluator ev(rt);
  std::cout << "CoreB kernel REPL. Exit with end-of-input.\n";
  std::string pending;
  for (;;) {
    std::cout << (pending.empty() ? "coreb> " : "  ...> ") << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) break;
    pending += line;
    pending += '\n';
    auto forms = reader::read_all(rt, pending);
    if (!forms) {
      const auto& err = forms.as_error();
      if (err.incomplete) continue;
      std::cout << "read error at line " << err.line << ", col " << err.col
                << ": " << err.message << '\n';
      pending.clear();
      continue;
    }
    pending.clear();
    for (const auto& form : *forms) {
      const auto v = ev.eval(form);
      if (!v) {
        std::cout << "error: " << v.as_error().message << '\n';
        break;
      }
      std::cout << v->print() << '\n';
    }
  }
  return 0;
}
