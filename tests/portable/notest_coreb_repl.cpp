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

#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "corvid/lang/coreb/coreb.h"
#include "corvid/strings/trimming.h"

using namespace corvid;
using namespace corvid::coreb;

namespace {

// The syntax the REPL is speaking.
enum class mode : std::uint8_t { monty, hall };

// Whether the line is blank: empty or spaces only.
[[nodiscard]] bool is_blank(std::string_view line) noexcept {
  return line.find_first_not_of(' ') == std::string_view::npos;
}

// Report a failure with its position.
void report(std::string_view what, const source_error& err) {
  std::cout << what << " error at line " << err.line << ", col " << err.col
            << ": " << err.message << '\n';
}

void print_help() {
  std::cout
      << "/help   show this list\n"
         "/monty  switch to Monty, the surface syntax\n"
         "/hall   switch to Hall, the native s-expression syntax\n"
         "/clear  reset the runtime, dropping all definitions\n"
         "/quit   exit (end-of-input also exits)\n"
         "In Monty, a blank line ends an indented block.\n";
}

// One interactive session: the shared runtime environment and the line
// loop's state.
//
// Both modes evaluate against the same runtime environment, so definitions
// persist across /monty and /hall switches. The environment is held by
// pointer so /clear can replace it wholesale, there being no way to empty
// one out; evaluators are transient views, so each batch constructs its
// own.
//
// Slash commands are recognized on any line, even mid-collection, so
// /clear and /quit can always rescue a stuck continuation; a mode switch
// or /clear abandons the partially entered input, while /help keeps it.
struct repl {
  std::unique_ptr<runtime_environment> run_env =
      std::make_unique<runtime_environment>();
  mode syntax = mode::monty;
  std::string pending;
  bool in_block{};

  // Read and process lines until /quit or end-of-input.
  void run() {
    for (;;) {
      std::cout << prompt() << std::flush;
      std::string line;
      if (!std::getline(std::cin, line)) break;
      // Piped CRLF input can reach here in binary mode; Monty rejects a raw
      // '\r'.
      if (line.ends_with('\r')) line.pop_back();
      const auto cmd = strings::trim(std::string_view{line});
      if (cmd.starts_with('/')) {
        if (!command(cmd)) break;
        continue;
      }
      const auto blank = is_blank(line);
      pending += line;
      pending += '\n';
      if (syntax == mode::monty)
        step_monty(blank);
      else
        step_hall();
    }
  }

  [[nodiscard]] std::string_view prompt() const noexcept {
    if (!pending.empty()) return syntax == mode::monty ? "  ...> " : " ...> ";
    return syntax == mode::monty ? "monty> " : "hall> ";
  }

  // Execute a slash command; false exits the loop.
  [[nodiscard]] bool command(std::string_view line) {
    if (line == "/quit") return false;
    if (line == "/help") {
      print_help();
    } else if (line == "/monty") {
      syntax = mode::monty;
      reset();
      std::cout << "Monty mode.\n";
    } else if (line == "/hall") {
      syntax = mode::hall;
      reset();
      std::cout << "Hall mode.\n";
    } else if (line == "/clear") {
      run_env = std::make_unique<runtime_environment>();
      reset();
      std::cout << "Runtime cleared.\n";
    } else {
      std::cout << "unknown command; /help lists them\n";
    }
    return true;
  }

  void reset() {
    pending.clear();
    in_block = false;
  }

  // Parse and run `pending` as Monty statements; a block in progress or
  // lexically open input keeps collecting instead.
  void step_monty(bool blank) {
    if (in_block && !blank) return;
    auto lexed = monty::lexer::lex(pending);
    if (!lexed) {
      const auto& err = lexed.as_error();
      if (err.incomplete()) return;
      report("lex", err);
      reset();
      return;
    }
    auto toks = *std::move(lexed);
    auto parsed = monty::statement_parser::parse_all(*run_env, toks);
    if (!parsed) {
      const auto& err = parsed.as_error();
      // A failure at the synthesized eof token is one more lines could
      // repair (a block header awaiting its body), so a non-blank line
      // opens a block; a failure anywhere earlier is already hard, and the
      // ending blank line reports whatever a block left broken.
      if (!blank && err.pos >= pending.size()) {
        in_block = true;
        return;
      }
      report("parse", err);
      reset();
      return;
    }
    reset();
    auto forms = *std::move(parsed);
    // Evaluation may collect at safe points; the not-yet-evaluated forms
    // are roots only while pinned.
    gc_pin pin(run_env->rt, forms);
    run_forms(forms, true);
  }

  // Read and run `pending` as Hall forms; incomplete input keeps
  // collecting instead.
  void step_hall() {
    auto forms = hall_reader::read_all(*run_env, pending);
    if (!forms) {
      const auto& err = forms.as_error();
      if (err.incomplete()) return;
      report("read", err);
      reset();
      return;
    }
    reset();
    gc_pin pin(run_env->rt, *forms);
    run_forms(*forms, false);
  }

  // Evaluate the pinned forms in order, printing each value; when
  // translating, each form is prefaced by its Hall form and the Monty the
  // unparser round-trips it to.
  void run_forms(std::span<const value> forms, bool translate) const {
    evaluator ev(*run_env);
    for (const auto& form : forms) {
      if (translate) {
        std::cout << "hall:  " << form.print() << '\n';
        std::cout << "monty: " << monty::unparser::unparse(*run_env, form)
                  << '\n';
      }
      const auto v = ev.eval(form);
      if (!v) {
        std::cout << "error: " << v.as_error().reason << '\n';
        break;
      }
      std::cout << v->print() << '\n';
    }
  }
};

} // namespace

// Minimal interactive REPL for CoreB, speaking both syntaxes over one shared
// runtime.
//
// Starts in Monty mode; slash commands steer it, recognized on any line,
// even mid-continuation: /monty and /hall pick the syntax, /clear resets
// the runtime, /help lists the commands, and /quit exits, as does
// end-of-input (Ctrl+Z then Enter on Windows, Ctrl+D elsewhere).
// Definitions persist across switches, both modes evaluating against the
// same runtime.
//
// Monty mode shows each statement's desugared Hall form and canonical Monty
// round trip before its value; Hall mode prints values only. Either way,
// input left lexically open (an unbalanced list, bracket, or Hall escape)
// continues onto the next line, and a Monty statement whose parse fails at
// the end of input (a block header, say) collects lines until a blank line
// ends the block, per Python's interactive precedent.
int main() {
  std::cout << "CoreB REPL, speaking Monty. /help lists the commands.\n";
  repl{}.run();
  return 0;
}
