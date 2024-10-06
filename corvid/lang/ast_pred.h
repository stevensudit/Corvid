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
#pragma once
#include <string_view>
#include <variant>
#include <vector>

#include "../containers/transparent.h"
#include "../enums/sequence_enum.h"
#include "../strings.h"

namespace corvid { inline namespace lang { namespace ast_pred {

// Predicate Abstract Syntax Tree (AST) and transforms.

// Any single value for a key.
using any_single_value = std::variant<std::monostate, std::string, int64_t>;

// Any value, included repeated ones.
using any_value = std::variant<std::monostate, any_single_value,
    std::vector<any_single_value>>;

// Key for lookup, or a literal value.
using key_or_value = std::variant<std::monostate, std::string, any_value>;

// Operations for AST predicates.
enum class operation {
  undefined,
  always_false,
  always_true,
  and_junction,
  or_junction,
  not_junction,
  exists,
  absent,
  eq,
  ne,
  lt,
  le,
  gt,
  ge,
  contains,
  starts_with,
  ends_with,
  matches,
};

}}} // namespace corvid::lang::ast_pred

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::lang::ast_pred::operation> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::lang::ast_pred::operation,
        "undefined, false, true, and, or, not, exists, absent, eq, ne, "
        "lt, le, gt, ge, contains, starts_with, ends_with, matches">();

namespace corvid { inline namespace lang { namespace ast_pred {

// AST predicate lookup.
//
// Returns `std::monostate` for missing keys.
struct lookup {
  virtual ~lookup() = default;
  virtual const any_value& operator[](const std::string& key) const;
  static inline const any_value missing;
};

// Simple map-based lookup.
struct map_lookup: public lookup {
  const any_value& operator[](const std::string& key) const override {
    const auto it = m.find(key);
    return it == m.end() ? missing : it->second;
  }

  string_map<any_value> m;
};

// TODO: See if forward declaration allows us to move the convenient usings up
// here.

// AST predicate node.
struct node: public std::enable_shared_from_this<node> {
protected:
  enum class allow { ctor };

public:
  explicit node(allow, operation op) : op{op} {}
  virtual ~node() = default;
  virtual bool eval(const lookup& lk) const {
    (void)lk;
    return false;
  };
  virtual bool append(std::string& out) const {
    strings::append_enum(out, op);
    return true;
  }
  std::string print() const {
    std::string out;
    append(out);
    return out;
  }

  const operation op;

  template<operation op, typename... Args>
  [[nodiscard]] static std::shared_ptr<node> make(Args&&... args);

  static bool print(std::string& out, const any_single_value& value) {
    if (std::holds_alternative<std::string>(value)) {
      strings::append(out, '"', std::get<std::string>(value), '"');
    } else if (std::holds_alternative<int64_t>(value)) {
      strings::append_num(out, std::get<int64_t>(value));
    } else {
      strings::append(out, "null");
    }
    return true;
  }

  static bool print(std::string& out, const any_value& value) {
    if (std::holds_alternative<any_single_value>(value)) {
      return print(out, std::get<any_single_value>(value));
    } else if (std::holds_alternative<std::vector<any_single_value>>(value)) {
      const auto& values = std::get<std::vector<any_single_value>>(value);
      strings::append(out, '[');
      for (const auto& v : values) {
        if (!print(out, v)) return false;
        strings::append(out, ", ");
      }
      if (!values.empty()) out.resize(out.size() - 2);
      strings::append(out, ']');
    } else {
      strings::append(out, "null");
    }
    return true;
  }

  static bool print(std::string& out, const key_or_value& value) {
    if (std::holds_alternative<std::string>(value)) {
      strings::append(out, std::get<std::string>(value));
    } else if (std::holds_alternative<any_value>(value)) {
      return print(out, std::get<any_value>(value));
    } else {
      strings::append(out, "null");
    }
    return true;
  }

  static bool
  print(std::string& out, const std::vector<std::shared_ptr<node>>& nodes) {
    strings::append(out, ":(");
    for (const auto& n : nodes) {
      n->append(out);
      strings::append(out, ", ");
    }
    if (!nodes.empty()) out.resize(out.size() - 2);
    strings::append(out, ')');
    return true;
  }
};

// Shared pointer to AST predicate root, internal, or leaf node.
using node_ptr = std::shared_ptr<node>;

// List of AST predicate nodes.
using node_list = std::vector<node_ptr>;

// Concept for a type that is derived from `node`.
template<typename T>
concept node_type = std::derived_from<std::remove_cvref_t<T>, node>;

// Concept for a shared_ptr to a T that is derived from `node`.
template<typename T>
concept node_ptr_type =
    std::is_same_v<std::shared_ptr<typename T::element_type>, T> &&
    node_type<typename T::element_type>;

// AST predicate junction.
//
// Stores junctions or leaf nodes. Op may be `and`, `or`, or `not`.
struct junction: public node {
  explicit junction(allow, operation op, node_list&& nodes = {})
      : node{allow::ctor, op}, nodes{std::move(nodes)} {}

  bool append(std::string& out) const override {
    node::append(out);
    return print(out, nodes);
  }

  node_list nodes;
};

struct and_node final: public junction {
  explicit and_node(allow, node_list&& nodes = {})
      : junction{allow::ctor, operation::and_junction, std::move(nodes)} {}

  template<node_ptr_type... Args>
  explicit and_node(allow, Args&&... args)
      : and_node{allow::ctor, node_list{std::forward<Args>(args)...}} {}
};

struct or_node final: public junction {
  explicit or_node(allow, node_list&& nodes = {})
      : junction{allow::ctor, operation::or_junction, std::move(nodes)} {}

  template<node_ptr_type... Args>
  explicit or_node(allow, Args&&... args)
      : or_node{allow::ctor, node_list{std::forward<Args>(args)...}} {}
};

struct not_node final: public junction {
  explicit not_node(allow, node_list&& nodes)
      : junction{allow::ctor, operation::not_junction, {std::move(nodes)}} {}

  template<node_ptr_type... Args>
  explicit not_node(allow, Args&&... args)
      : not_node{allow::ctor, node_list{std::forward<Args>(args)...}} {}
};

struct true_node final: public node {
  true_node(allow) : node{allow::ctor, operation::always_true} {}
  bool eval(const lookup&) const override { return true; }
};

struct false_node final: public node {
  false_node(allow) : node{allow::ctor, operation::always_false} {}
  bool eval(const lookup&) const override { return false; }
};

struct unary_leaf: public node {
  unary_leaf(allow, operation op, key_or_value&& value)
      : node{allow::ctor, op}, value{std::move(value)} {}

  bool append(std::string& out) const override {
    node::append(out);
    strings::append(out, ":(");
    node::print(out, value);
    strings::append(out, ')');
    return true;
  }

  key_or_value value;
};

struct binary_leaf: public node {
  binary_leaf(allow, operation op, key_or_value&& lhs, key_or_value&& rhs)
      : node{allow::ctor, op}, lhs{std::move(lhs)}, rhs{std::move(rhs)} {}

  bool append(std::string& out) const override {
    node::append(out);
    strings::append(out, ":(");
    node::print(out, lhs);
    strings::append(out, ", ");
    node::print(out, rhs);
    strings::append(out, ')');
    return true;
  }

  key_or_value lhs;
  key_or_value rhs;
};

struct eq_node final: public binary_leaf {
  eq_node(allow, key_or_value&& lhs, key_or_value&& rhs)
      : binary_leaf{allow::ctor, operation::eq, std::move(lhs),
            std::move(rhs)} {}
};

struct ne_node final: public binary_leaf {
  ne_node(allow, key_or_value&& lhs, key_or_value&& rhs)
      : binary_leaf{allow::ctor, operation::ne, std::move(lhs),
            std::move(rhs)} {}
};

struct exists_node final: public unary_leaf {
  exists_node(allow, key_or_value&& value)
      : unary_leaf{allow::ctor, operation::exists, std::move(value)} {}
};

struct absent_node final: public unary_leaf {
  absent_node(allow, key_or_value&& value)
      : unary_leaf{allow::ctor, operation::absent, std::move(value)} {}
};

template<operation op, typename... Args>
std::shared_ptr<node> node::make(Args&&... args) {
  if constexpr (op == operation::and_junction) {
    return std::make_shared<and_node>(allow::ctor,
        std::forward<Args>(args)...);
  } else if constexpr (op == operation::or_junction) {
    return std::make_shared<or_node>(allow::ctor, std::forward<Args>(args)...);
  } else if constexpr (op == operation::not_junction) {
    return std::make_shared<not_node>(allow::ctor,
        std::forward<Args>(args)...);
  } else if constexpr (op == operation::always_false) {
    return std::make_shared<false_node>(allow::ctor);
  } else if constexpr (op == operation::always_true) {
    return std::make_shared<true_node>(allow::ctor);
  } else if constexpr (op == operation::eq) {
    return std::make_shared<eq_node>(allow::ctor, std::forward<Args>(args)...);
  } else if constexpr (op == operation::ne) {
    return std::make_shared<ne_node>(allow::ctor, std::forward<Args>(args)...);
  } else if constexpr (op == operation::exists) {
    return std::make_shared<exists_node>(allow::ctor,
        std::forward<Args>(args)...);
  } else if constexpr (op == operation::absent) {
    return std::make_shared<absent_node>(allow::ctor,
        std::forward<Args>(args)...);
  }
}

// Non-member wrapper; still type-safe because it takes `operation`.
template<operation op, typename... Args>
[[nodiscard]] node_ptr make(Args&&... args) {
  return node::make<op>(std::forward<Args>(args)...);
}

// Disjunctive Normal Form (DNF) conversion.
//
// Performs some optimizations and simplifications.
class dnf {
public:
  static node_ptr convert(const node_ptr& root) { return handle(root); }

private:
  // Recursively rebuild subtree from this root down.
  static node_ptr handle(const node_ptr& root) {
    switch (root->op) {
    case operation::and_junction:
      return handle_conjunction(
          std::dynamic_pointer_cast<and_node>(root)->nodes);
    case operation::or_junction:
      return handle_disjunction(
          std::dynamic_pointer_cast<or_node>(root)->nodes);
    case operation::not_junction:
      return handle_negation(
          std::dynamic_pointer_cast<not_node>(root)->nodes[0]);
    default: return root;
    }
  }

  static node_ptr handle_negation(const node_ptr& root) {
    switch (root->op) {
    case operation::always_false: return make<operation::always_true>();
    case operation::always_true: return make<operation::always_false>();
    case operation::not_junction: {
      // Nested NOTs cancel out.
      auto r = std::dynamic_pointer_cast<not_node>(root);
      return handle(r->nodes.front());
    }
    case operation::and_junction: {
      // De Morgan's Law: NOT(A AND B) = NOT(A) OR NOT(B)
      auto inner_and = std::dynamic_pointer_cast<and_node>(root);
      node_list new_nodes;
      for (const auto& n : inner_and->nodes)
        new_nodes.push_back(handle_negation(n));
      return make<operation::or_junction>(std::move(new_nodes));
    }
    case operation::or_junction: {
      // De Morgan's Law: NOT(A OR B) = NOT(A) AND NOT(B)
      auto inner_or = std::dynamic_pointer_cast<or_node>(root);
      node_list new_nodes;
      for (const auto& n : inner_or->nodes) {
        new_nodes.push_back(handle_negation(n));
      }
      return make<operation::and_junction>(std::move(new_nodes));
    }
    case operation::eq: {
      // NOT(A = B) = A != B
      auto r = std::dynamic_pointer_cast<eq_node>(root);
      return make<operation::ne>(key_or_value{r->lhs}, key_or_value{r->rhs});
    }
    case operation::ne: {
      // NOT(A != B) = A = B
      auto r = std::dynamic_pointer_cast<ne_node>(root);
      return make<operation::eq>(key_or_value{r->lhs}, key_or_value{r->rhs});
    }
    case operation::exists: {
      // NOT(EXISTS A) = ABSENT A
      auto r = std::dynamic_pointer_cast<exists_node>(root);
      return make<operation::absent>(key_or_value{r->value});
    }
    case operation::absent: {
      // NOT(ABSENT A) = EXISTS A
      auto r = std::dynamic_pointer_cast<absent_node>(root);
      return make<operation::exists>(key_or_value{r->value});
    }
    default: {
      return make<operation::not_junction>(handle(root));
    }
    };
  }

  // Accumulate distributions.
  //
  // On input, `distribution` is a list of AND nodes (that will ultimately be
  // ORed together). Distributes `source` against these, returning a new list
  // of AND nodes, multiplied by the size of `source`.
  static node_list distribute_or_values(const node_list& distribution,
      const std::shared_ptr<or_node>& source) {
    node_list accumulated;

    // Distribute each `or_child` across the `distribution`.
    for (const auto& or_child : source->nodes) {
      for (const auto& dist_child : distribution) {
        // Otherwise, just add the two nodes.
        // TODO: Try to do this with initializers or something.
        node_list new_nodes;
        auto inner_and = std::dynamic_pointer_cast<and_node>(dist_child);
        for (const auto& node : inner_and->nodes) {
          // Flatten nested ANDs.
          if (node->op == operation::and_junction) {
            auto inner_inner_and = std::dynamic_pointer_cast<and_node>(node);
            new_nodes.insert(new_nodes.end(), inner_inner_and->nodes.begin(),
                inner_inner_and->nodes.end());
          } else
            new_nodes.push_back(node);
        }
        new_nodes.push_back(or_child);
        accumulated.push_back(
            make<operation::and_junction>(std::move(new_nodes)));
      }
    }
    return accumulated;
  }

  // Handle the children of an AND.
  static node_ptr handle_conjunction(const node_list& nodes) {
    // Convert each node, splitting into a list of OR's and the rest.
    node_list converted_nodes, converted_or_nodes;
    for (const auto& n : nodes) {
      auto converted = convert(n);
      // An always-true node cannot contribute to the result.
      if (converted->op == operation::always_true) continue;
      // An always-false node will always result in false.
      if (converted->op == operation::always_false)
        return make<operation::always_false>();
      // Target for special handling if an OR is found.
      if (converted->op == operation::or_junction) {
        converted_or_nodes.push_back(std::move(converted));
        continue;
      }
      // Flatten nested ANDs.
      if (converted->op == operation::and_junction) {
        auto inner_and = std::dynamic_pointer_cast<and_node>(converted);
        converted_nodes.insert(converted_nodes.end(), inner_and->nodes.begin(),
            inner_and->nodes.end());
      } else {
        converted_nodes.push_back(std::move(converted));
      }
    }

    // If no nodes, then the result is always true.
    if (converted_nodes.empty() && converted_or_nodes.empty())
      return make<operation::always_true>();

    // If just one, then use that.
    if (converted_nodes.size() == 1 && converted_or_nodes.size() == 0)
      return converted_nodes.front();
    if (converted_nodes.size() == 0 && converted_or_nodes.size() == 1)
      return converted_or_nodes.front();

    // If no OR nodes to distribute, just recreate the AND node with the
    // converted children.
    if (converted_or_nodes.empty())
      return make<operation::and_junction>(std::move(converted_nodes));

    // Distribute OR over AND
    // Start with a single AND node with all of the non-OR nodes. This will be
    // multiplied by the children of the OR nodes.
    node_list accumulated;
    accumulated.push_back(
        make<operation::and_junction>(std::move(converted_nodes)));

    // Distribute the OR nodes over the other nodes, iteratively.
    for (const auto& converted_or_node : converted_or_nodes) {
      auto inner_or = std::dynamic_pointer_cast<or_node>(converted_or_node);
      accumulated = distribute_or_values(accumulated, inner_or);
    }

    return make<operation::or_junction>(std::move(accumulated));
  }

  // Handle the children of an OR.
  static node_ptr handle_disjunction(const node_list& nodes) {
    // Build converted list, scanning for the types created.
    node_list converted_nodes;
    for (const auto& n : nodes) {
      auto converted = convert(n);
      // An always-false node cannot contribute to the result.
      if (converted->op == operation::always_false) continue;
      // An always-true node will always result in true.
      if (converted->op == operation::always_true)
        return make<operation::always_true>();
      // Flatten nested ORs.
      if (converted->op == operation::or_junction) {
        auto inner_or = std::dynamic_pointer_cast<or_node>(converted);
        for (const auto& child : inner_or->nodes)
          converted_nodes.push_back(child);
      } else
        converted_nodes.push_back(std::move(converted));
    }

    // If no nodes, then the result is always false.
    if (converted_nodes.empty()) return make<operation::always_false>();
    // If just one, then use that.
    if (converted_nodes.size() == 1) return converted_nodes.front();

    // Don't distribute these terms because that would move us towards CNF, not
    // DNF.
    return make<operation::or_junction>(std::move(converted_nodes));
  }
};
}}} // namespace corvid::lang::ast_pred

// TODO: Properly register the variants so that they can be printed as JSON
// without all of these helper functions.
// TODO: Turn tautologies into always-true or always-false nodes. This is easy
// for always_true and always_false, but we need to be able to determine if an
// equality is mutually exclusive.
