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
#include "lite.h"
#include "../enums.h"

// Recommendation: While you can import the entire `corvid::strings` namespace,
// you may not want to bring in all of these symbols, or you may wish to do so
// more selectively.
//
// The way to do that is to import just `corvid` and then reference symbols
// through the `strings` namespace, such as `strings::trim_braces`. You can
// also choose to import the inline namespace for that group of symbols, such
// as `corvid::strings::appending`.
namespace corvid::strings {
inline namespace registration {

//
// Append plugin
//

// Register an `append` function for a type.
//
// This is the moral equivalent of writing an overload for the `append`
// functions, but has the advantage of actually working. The reason you can't
// just write overloads (of a function that appends a single part) is that the
// various multi-part functions forward to single-part ones, but they can only
// do this to functions that were previously defined. So if you define a
// function later, it does not participate in overload resolution.
//
// The rules for partial specialization of templated values are different and
// more flexible, so this technique takes advantage of them.
//
// Concretely, your function has to fit the signature but is otherwise
// unrestricted. It can be a class static function, a free function, or even a
// lambda.
//
// Note that there is a separate mechanism for enums, so don't use this for
// them.
//
// For example:
//
//    template<AppendTarget A>
//    static auto& append(A& target, const person& p) {
//      return corvid::strings::append(target, p.last, ", ", p.first);
//    }
//
//    template<AppendTarget A>
//    constexpr auto corvid::strings::append_override_fn<A, person> =
//        person::append<A>;
//

// General case to allow specialization.
template<AppendTarget A, typename T>
constexpr auto append_override_fn = nullptr;

// Concept for types that have an overridden appender registered.
template<typename T>
concept AppendableOverridden =
    (!std::is_null_pointer_v<decltype(append_override_fn<std::string, T>)>);

// Concept for types that registered as a stream appendable. This means that we
// append them by using the `std::ostream::operator<<`.
template<typename T>
concept StreamAppendable = stream_append_v<T>;

// Concept for types that are not registered for special handling.
//
// Note that, despite the name, types that fit this concept are only
// *candidates* for native appending, not guaranteed to be appendable. This is
// always a partial, necessary requirement, not a sufficient one.
template<typename T>
concept Appendable = (!AppendableOverridden<T>)&&(!StreamAppendable<T>);

} // namespace registration
inline namespace existing {
// We want to be able to check if something exists by using it as a predicate,
// whether it's a pointer or an optional or something similar, but this angers
// gcc, so we isolate it.
PRAGMA_GCC_DIAG(push);
PRAGMA_GCC_IGNORED("-Waddress");
PRAGMA_GCC_IGNORED("-Wnonnull-compare");
[[nodiscard]] inline constexpr bool is_present(const BoolLike auto& p) {
  return (p) ? true : false;
}
PRAGMA_GCC_DIAG(pop);

// If it's not like a bool, then it's always present.
template<typename T>
requires(!BoolLike<T>)
[[nodiscard]] inline constexpr bool is_present(const T&) {
  return true;
}
} // namespace existing
inline namespace appending {

//
// Append, Concat, and Join
//

// The `append`, `append_join`, and `append_join_with` functions take an
// AppendTarget, `target`, which can be a `std::string` or any `std::ostream`,
// as the first parameter and append the rest to it.
//
// The `concat`, `join`, and `join_with` functions take the pieces and return
// the whole as a string.
//
// For the `append_join_with` and `join_with` functions, the parameter right
// after `target` is interpreted as the delimiter to separate the other values
// *with*. The `append_join` and `join` functions instead default the delimiter
// to ", ".
//
// All of the joining functions can have `join_opt` specified to control
// such things as whether container elements are surrounded with appropriate
// braces, whether keys should be shown for containers, whether strings should
// be quoted, and whether a delimiter should be emitted at the start; see enum
// definition below for description.
//
// The supported types for the pieces include: `std::string_view`,
// `std::string`, `const char*`, `char`, `bool`, `int`, `double`, `enum`, and
// containers.
//
// Containers include `std::pair`, `std::tuple` and anything you can do a
// ranged-for over, such as `std::vector`. For keyed containers, such as
// `std::map`, only the values are used, unless `join_opt` specifies otherwise.
// Containers may be nested arbitrarily.
//
// In addition to `int` and `double`, all other native numeric types are
// supported.
//
// Pointers and `std::optional` are dereferenced if a value is available. To
// instead show the address of a pointer in hex, cast it to `void*`.
//
// Any other class can be supported by registering an `append_override_fn`
// callback (and, if it needs to support internal delimiters, an
// `append_join_override_fn` callback).
//
// If the class can already stream out to `std::ostream`, you can support it
// for append just by enabling `stream_append_v`.

// Append one stringlike thing to `target`. If it's not present, append "null".
template<StringViewConvertible T>
requires Appendable<T>
constexpr auto& append(AppendTarget auto& target, const T& part) {
  auto a = appender{target};
  if (is_present(part))
    a.append(std::string_view{part});
  else
    a.append("null"sv);
  return target;
}

// Append `nullptr_t` to `target` as "null".
constexpr auto& append(AppendTarget auto& target, NullPtr auto) {
  return append(target, (char*){});
}

// Append one integral number to target, allowing `base`, `width`, and `pad` to
// be specified. Also handles `char` and `bool`, appending "true" or "false"
// for the latter.
template<int base = 10, size_t width = 0, char pad = ' ', std::integral T>
constexpr auto& append(AppendTarget auto& target, T part) {
  if constexpr (Char<T>)
    appender{target}.append(part);
  else if (Bool<T>)
    appender{target}.append(part ? "true"sv : "false"sv);
  else
    append_num<base, width, pad>(target, part);
  return target;
}

// Append one floating-point number to `target`. When called directly, `fmt`,
// `precision`, `width`, and `pad` may be specified.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' '>
constexpr auto&
append(AppendTarget auto& target, std::floating_point auto part) {
  return append_num<fmt, precision, width, pad>(target, part);
}

// Append one pointer or optional value to `target`. If not present, appends
// `null`.
template<OptionalLike T>
requires Appendable<T>
constexpr auto& append(AppendTarget auto& target, const T& part) {
  if (part) return append(target, *part);
  return append(target, nullptr);
}

// Append one void pointer, as hex, to `target`. Note that null is rendered as
// zeroes.
constexpr auto&
append(AppendTarget auto& target, const VoidPointer auto& part) {
  return append<16>(target, reinterpret_cast<uintptr_t>(part));
}

// Append one scoped or unscoped `enum` to `target`.
//
// Unscoped `enum`s are appended as their underlying type.
template<StdEnum T>
constexpr auto& append(AppendTarget auto& target, const T& part) {
  if constexpr (ScopedEnum<T>)
    return append_enum(target, part);
  else
    return append(target, as_underlying(part));
}

// Append one container, as its element values, to `target` without
// delimiters.  See `append_join_with` for delimiter support. When called
// directly, `keyed` may be specified to extract the key/value pair instead of
// just the value.
template<bool keyed = false, Container T>
requires Appendable<T>
constexpr auto& append(AppendTarget auto& target, const T& parts) {
  for (auto& part : parts) append(target, element_value<keyed>(part));
  return target;
}

// Append one monostate to `target`, as "null".
constexpr auto& append(AppendTarget auto& target, const MonoState auto&) {
  return append(target, nullptr);
}

// Append one variant to `target`, as its current type. If the variant is
// valueless, appends "null".
template<Variant T>
requires Appendable<T>
constexpr auto& append(AppendTarget auto& target, const T& part) {
  if (!part.valueless_by_exception())
    std::visit([&target](auto&& inside) { append(target, inside); }, part);
  else
    append(target, nullptr);
  return target;
}

// Append a `StreamAppendable` to `target`.
constexpr auto&
append(AppendTarget auto& target, const StreamAppendable auto& part) {
  append_stream(target, part);
  return target;
}

// Append pieces to `target` without delimiters. See `append_join_with` for
// delimiter support.
constexpr auto& append(AppendTarget auto& target, const auto& head,
    const auto& middle, const auto&... tail) {
  append(append(target, head), middle);
  if constexpr (sizeof...(tail) != 0) append(target, tail...);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`
// without delimiters. See `append_join_with` for delimiter support.
template<TupleLike T>
requires Appendable<T>
constexpr auto& append(AppendTarget auto& target, const T& parts) {
  std::apply(
      [&target](const auto&... parts) {
        if constexpr (sizeof...(parts) != 0) append(target, parts...);
      },
      parts);
  return target;
}

// Append one append-overridden object to `target`.
//
// Note: The syntax on this is very sensitive, so any change might break it.
template<AppendTarget A, AppendableOverridden T>
constexpr auto& append(A& target, const T& part) {
  auto& fn = strings::append_override_fn<A, T>;
  fn(target, part);
  return target;
}

// Concatenate pieces together into `std::string` without delimiters. See
// `join` and `join_with` for delimiter support.
[[nodiscard]] constexpr auto concat(const auto& head, const auto&... tail) {
  std::string target;
  return append(target, head, tail...);
}

// Determine if `c` needs to be escaped.
[[nodiscard]] inline bool needs_escaping(char c) noexcept {
  return (c == '"' || c == '\\' || c == '/' || c < 32);
}

// Determine if `s` needs to be escaped.
[[nodiscard]] inline bool needs_escaping(std::string_view s) noexcept {
  for (auto c : s)
    if (needs_escaping(c)) return true;
  return false;
}

// Append one string to `target`, escaping as needed for JSON.
inline auto& append_escaped(AppendTarget auto& target, std::string_view part) {
  if (!needs_escaping(part)) return append(target, part);

  auto a = appender{target};
  for (auto c : part) {
    if (needs_escaping(c)) {
      a.append('\\');
      switch (c) {
      case '"': [[fallthrough]];
      case '\\': [[fallthrough]];
      case '/': a.append(c); break;
      case '\b': a.append('b'); break;
      case '\f': a.append('f'); break;
      case '\n': a.append('n'); break;
      case '\r': a.append('r'); break;
      case '\t': a.append('t'); break;
      default:
        a.append('u');
        append<16, 4, '0'>(target, static_cast<uint16_t>(c));
        break;
      }
    } else {
      a.append(c);
    }
  }

  return target;
}

} // namespace appending
inline namespace joinoptions {

//
// Join options
//

// Join option bitmask flags.
//
// The default behavior shows braces around containers and displays only values
// in them, not keys. The braces can be suppressed with `flat`, and the keys
// can be shown with `keyed`. The `quoted` option will show quotes around
// strings, and `prefixed` will prefix the delimiter to the start of each part.
//
// The `flat_keyed` option is a convenience for `flat | keyed`, which provides
// a no-frills dump of everything. The `json` option is a convenience for
// `braced | keyed | quoted`, which selects something very close to JSON.
//
// Note that, since we can't know what was streamed out before, we can't tell
// that we need to delimit unless we're writing multiple parts ourselves. The
// caller would have to specify this delimiting with the `prefix` option, if
// that's what they want.
enum class join_opt {
  // braced - Show braces around containers; the default behavior.
  braced = 0,
  // flat - Avoid showing braces around containers.
  flat = 1,
  // keyed - Show keys in containers, in addition to values.
  keyed = 2,
  // quoted - Show quotes around strings (using `open` and `close`).
  quoted = 4,
  // prefixed - Prefix with the delimiter.
  prefixed = 8,
  // Convenience aliases:
  // flat-keyed.
  flat_keyed = flat | keyed,
  // json - Show as JSON.
  json = braced | keyed | quoted,
  // Max value
  all = braced | flat | keyed | quoted | prefixed,
};

} // namespace joinoptions
} // namespace corvid::strings

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::strings::joinoptions::join_opt> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::strings::joinoptions::join_opt,
        corvid::strings::joinoptions::join_opt::all>();

namespace corvid::strings {
inline namespace registration {

//
// Append join plugin
//

// Register an `append_join` function for a type.
//
// See `append_override_fn`, above, for explanations. Note that you may wish to
// use the helpers in the `decode` namespace for more sophisticated
// functionality.
//
// For example:
//
//      template<auto opt = strings::join_opt::braced, char open = 0,
//          char close = 0, AppendTarget A>
//      static A& append_join_with(A& target, strings::delim d,
//          const person& p) {
//        return corvid::strings::append_join_with<opt, open, close>(
//            target, d, p.last, ", ", p.first);
//      }
//
//    template<strings::join_opt opt, char open, char close, AppendTarget A>
//        constexpr auto strings::append_join_override_fn<opt, open, close, A,
//        person> = person::append_join_with<opt, open, close, A>;
//
// Note: You will want to specialize `append_override_fn` as well.

// General case to allow specialization.
template<join_opt opt, char open, char close, AppendTarget A, typename T>
constexpr auto append_join_override_fn = nullptr;

// Concept for types that have an overridden join appender registered.
template<typename T>
concept JoinAppendableOverridden =
    (!std::is_null_pointer_v<decltype(append_join_override_fn<join_opt{}, ' ',
            ' ', std::string, T>)>);

// Concept for types that do not have an overridden join appender registered.
//
// Note that, despite the name, types that fit this concept are only
// *candidates* for native join appending, not guaranteed to be appendable.
// This is always a partial, necessary requirement, not a sufficient one.
template<typename T>
concept JoinAppendable = (!JoinAppendableOverridden<T>) || Appendable<T>;

} // namespace registration
namespace decode {
using namespace corvid::bitmask;

//
// Decode
//

// Determine whether to add braces.
// Logic: Unless braces are suppressed, use braces when we have them.
template<join_opt opt, char open, char close>
constexpr bool braces_v = missing(opt, join_opt::flat) && (open && close);

// Calculate next opt for head part.
// Logic: No need to emit leading delimiter for head, since we've already
// emitted it if it was needed.
template<join_opt opt>
constexpr join_opt head_opt_v = clear(opt, join_opt::prefixed);

// Calculate next opt for next part of tail.
// Logic: After the head, we need to emit leading delimiters before parts.
template<join_opt opt>
constexpr join_opt next_opt_v = set(opt, join_opt::prefixed);

// Determine whether to show keys.
template<join_opt opt>
constexpr bool keyed_v = has(opt, join_opt::keyed);

// Determine whether to show quotes.
template<join_opt opt>
constexpr bool quoted_v = has(opt, join_opt::quoted);

// Determine whether to lead with a delimiter.
template<join_opt opt>
constexpr bool delimit_v = has(opt, join_opt::prefixed);

// Determine whether to show as JSON.
template<join_opt opt>
constexpr bool json_v =
    has_all(opt, join_opt::json) && !has(opt, join_opt::flat);

} // namespace decode
inline namespace joining {

//
// Join
//
//

// Append one piece to `target`, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    JoinAppendable T>
requires(!Container<T>) && (!Variant<T>) && (!OptionalLike<T>) &&
        (!TupleLike<T>)
constexpr auto& append_join_with(AppendTarget auto& target, delim d,
    const T& part) {
  constexpr bool add_braces = decode::braces_v<opt, open, close>;
  constexpr bool add_quotes =
      (StringViewConvertible<T> || StdEnum<T>)&&decode::quoted_v<opt>;
  bool not_null = is_present(part);

  d.append_if<decode::delimit_v<opt>>(target);

  if constexpr (add_braces) append(target, open);

  if constexpr (add_quotes) {
    if (not_null) {
      append(target, '"');
      if constexpr (StdEnum<T>)
        append(target, part);
      else
        append_escaped(target, std::string_view{part});
      append(target, '"');
    } else {
      append(target, part);
    }
  } else {
    append(target, part);
  }

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one pointer or optional value to `target`, joining with `delim`. If
// not present, appends `null`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    OptionalLike T>
requires JoinAppendable<T>
constexpr auto&
append_join_with(AppendTarget auto& target, delim d, const T& part) {
  if (part) return append_join_with<opt>(target, d, *part);
  return append(target, nullptr);
}

// Append one variant to `target`, as its current type, joining with `delim`.
// If the variant is valueless, appends "null".
template<auto opt = join_opt::braced, char open = 0, char close = 0, Variant T>
requires JoinAppendable<T>
constexpr auto&
append_join_with(AppendTarget auto& target, delim d, const T& part) {
  if (!part.valueless_by_exception())
    std::visit(
        [&target, &d](auto&& part) { append_join_with<opt>(target, d, part); },
        part);
  else
    append(target, nullptr);
  return target;
}

// Append one `std::pair`, as its elements, to `target`, joining with `delim`.
// Supports join_opt::json by emitting `"key": value`.
template<auto opt = join_opt::braced, char open = 0, char close = 0, StdPair T>
requires JoinAppendable<T>
constexpr auto&
append_join_with(AppendTarget auto& target, delim d, const T& part) {
  if (!decode::keyed_v<opt>)
    return append_join_with<opt, open, close>(target, d, part.second);

  constexpr auto is_json = decode::json_v<opt>;
  constexpr auto head_opt = decode::head_opt_v<opt>;
  constexpr auto next_opt = is_json ? head_opt : decode::next_opt_v<opt>;
  constexpr char next_open = open ? open : (is_json ? 0 : '{');
  constexpr char next_close = close ? close : (is_json ? 0 : '}');
  constexpr bool add_quotes =
      is_json && !StringViewConvertible<decltype(part.first)>;
  // TODO: Should we add !Container and so on?
  // TODO: What if the key is null?

  d.append_if<decode::delimit_v<opt>>(target);

  constexpr bool add_braces = decode::braces_v<opt, next_open, next_close>;
  if constexpr (add_braces) append(target, next_open);

  constexpr delim dq{"\""};
  dq.append_if<add_quotes>(target);
  // TODO: Test quote-encoding when a non-string is wrapped in quotes. We may
  // need to print to a string first, unless we know it's safe, such as a
  // number.
  append_join_with<head_opt>(target, d, part.first);
  dq.append_if<add_quotes>(target);
  constexpr delim ds{": "};
  ds.append_if<is_json>(target);
  append_join_with<next_opt>(target, d, part.second);

  if constexpr (add_braces) append(target, next_close);

  return target;
}

// Append one `std::tuple`, as its elements, to `target`, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    StdTuple T>
requires JoinAppendable<T>
constexpr auto&
append_join_with(AppendTarget auto& target, delim d, const T& part) {
  constexpr char next_open = open ? open : '{';
  constexpr char next_close = close ? close : '}';
  std::apply(
      [&target, &d](const auto&... parts) {
        if constexpr (sizeof...(parts) != 0)
          append_join_with<opt, next_open, next_close>(target, d, parts...);
      },
      part);
  return target;
}

// Append one append-overridden object to `target`, joining with `delim`.
//
// Note: The syntax on this is very sensitive, so any change might break it.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    AppendTarget A, JoinAppendableOverridden T>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  auto& fn = strings::append_join_override_fn<opt, open, close, A, T>;
  fn(target, d, part);
  return target;
}

// Append one container, as its element values, to `target`, joining with
// `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    Container T>
requires JoinAppendable<T>
constexpr auto&
append_join_with(AppendTarget auto& target, delim d, const T& parts) {
  constexpr auto head_opt = decode::head_opt_v<opt>;
  constexpr auto next_opt = decode::next_opt_v<opt>;
  constexpr bool is_keyed =
      decode::keyed_v<opt> && StdPair<decltype(*cbegin(parts))>;
  constexpr bool is_obj = is_keyed && decode::json_v<opt>;
  constexpr char next_open = open ? open : (is_obj ? '{' : '[');
  constexpr char next_close = close ? close : (is_obj ? '}' : ']');
  constexpr bool add_braces = decode::braces_v<opt, next_open, next_close>;

  d.append_if<decode::delimit_v<opt>>(target);
  if constexpr (add_braces) append(target, next_open);

  if (auto b = std::cbegin(parts), e = std::cend(parts); b != e) {
    append_join_with<head_opt>(target, d, container_element_v<is_keyed>(b));

    for (++b; b != e; ++b)
      append_join_with<next_opt>(target, d, container_element_v<is_keyed>(b));
  }

  if constexpr (add_braces) append(target, next_close);
  return target;
}

namespace details {
// Helper for `append_join_with` parameter pack overload.
template<join_opt opt, char open = 0, char close = 0, typename Head,
    typename... Tail>
constexpr auto& ajwh(AppendTarget auto& target, delim d, const Head& head,
    const Tail&... tail) {
  append_join_with<opt>(target, d, head);
  if constexpr (sizeof...(tail) != 0) ajwh<opt>(target, d, tail...);
  return target;
}
} // namespace details

// Append pieces to `target`, joining with `delim`.
// Passing -1 for `open` or `close` will suppress braces at the top level.
template<auto opt = join_opt::braced, char open = 0, char close = 0>
constexpr auto& append_join_with(AppendTarget auto& target, delim d,
    const auto& head, const auto& middle, const auto&... tail) {
  constexpr bool is_keyed = decode::keyed_v<opt> && StdPair<decltype(head)>;
  constexpr bool is_obj = is_keyed && decode::json_v<opt>;
  constexpr char next_open = open ? open : (is_obj ? '{' : '[');
  constexpr char next_close = close ? close : (is_obj ? '}' : ']');
  constexpr bool add_braces =
      decode::braces_v<opt, next_open, next_close> && open >= 0 && close >= 0;
  constexpr auto head_opt = decode::head_opt_v<opt>;
  constexpr auto next_opt = decode::next_opt_v<opt>;

  d.append_if<decode::delimit_v<opt>>(target);
  if constexpr (add_braces) append(target, next_open);

  append_join_with<head_opt>(target, d, head);
  append_join_with<next_opt>(target, d, middle);
  if constexpr (sizeof...(tail) != 0)
    details::ajwh<next_opt>(target, d, tail...);

  if constexpr (add_braces) append(target, next_close);
  return target;
}

// Append pieces to `target`, joining with a comma and space delimiter.
template<auto opt = join_opt::braced, char open = 0, char close = 0>
constexpr auto&
append_join(AppendTarget auto& target, const auto& head, const auto&... tail) {
  return append_join_with<opt, open, close>(target, delim{", "sv}, head,
      tail...);
}

// Join pieces together, with `delim`, into `std::string`.
template<auto opt = join_opt::braced, char open = 0, char close = 0>
[[nodiscard]] constexpr auto
join_with(delim d, const auto& head, const auto&... tail) {
  std::string target;
  return append_join_with<opt, open, close>(target, d, head, tail...);
}

// Join pieces together, comma-delimited, into `std::string`.
template<auto opt = join_opt::braced, char open = 0, char close = 0>
[[nodiscard]] constexpr auto join(const auto& head, const auto&... tail) {
  std::string target;
  return append_join_with<opt, open, close>(target, delim{", "sv}, head,
      tail...);
}

// Append pieces to target as JSON.
constexpr auto
append_json(AppendTarget auto& target, const auto& head, const auto&... tail) {
  return append_join_with<join_opt::json>(target, delim{", "sv}, head,
      tail...);
}

// Join pieces together into `std::string` as JSON.
[[nodiscard]] constexpr auto join_json(const auto& head, const auto&... tail) {
  std::string target;
  return append_join_with<join_opt::json>(target, delim{", "sv}, head,
      tail...);
}

} // namespace joining
} // namespace corvid::strings

//
// TODO
//

// TODO: Add method that takes pieces and counts up their total (estimated)
// size, for the purpose of reserving target capacity. Possibly add this an a
// `join_opt` for use in `concat` and `join`.

// TODO: Benchmark delim `find` single-char optimizations, to make sure they're
// faster.

// TODO: Consider offering a way to register a tuple-view function for use in
// `append_join`. The function would return a view of the object's contents as
// a tuple of references (perhaps with the aid of a helper), and the presence
// of this function would pick out the function override. It might also be
// helpful to be able to specify an internal delimiter. It's not clear that any
// of this is useful for straight-up `append`, though.

// TODO: Consider offering the reverse of `stream_append_v`. This would be
// named something like `stream_using_append_v`, and would enable an
// `operator<<` overload for the type, implemented by calling `append` (or
// maybe even `append_join`) on it.
