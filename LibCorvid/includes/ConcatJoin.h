// Corvid: A general-purpose C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022 Steven Sudit
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
#include "StringUtils.h"
#include "BitmaskEnum.h"

// Recommendation: While you can import the entire `corvid::strings` namespace,
// you may not want to bring in all of these symbols, or you may wish to do so
// more selectively.
//
// The way to do that is to import just `corvid` and then reference symbols
// through the `strings` namespace, such as `strings::trim_braces`. You can
// also choose to import the inline namespace for that group of symbols, such
// as `corvid::bracing`.
namespace corvid::strings {
inline namespace appendplugin {

//
// Append plugin
//

// Register an `append` function for a type.
//
// This is the moral equivalent of writing an overload for the `append
// functions, but has the advantage of actually working. The reason you can't
// just write overloads of a function that appends a single part is that the
// various multi-part functions forward to single-part functions, but they can
// only do this to functions that were previously defined. So if you define a
// function later, it does not participate in overload resolution.
//
// The rules for partial specialization of templated values are different and
// more flexible, so this technique takes advantage of them.
//
// Concretely, your function has to fit the signature but is otherwise
// unrestricted. It can be a class static function, a free function, or even a
// lambda.
//
// For example:
//
//    template<typename A>
//    static A& append(A& target, const person& p) {
//      return corvid::strings::append(target, p.last, ", ", p.first);
//    }
//
//    template<typename A>
//    constexpr auto corvid::strings::append_override_fn<A, person> =
//        person::append<A>;
//
// Note: You may also need to specialize `append_join_override_fn`; see below.

// General case to allow specialization.
template<typename A, typename T>
constexpr auto append_override_fn = nullptr;

// Whether an `append_override_fn` is specialized on `T`.
template<typename T>
constexpr bool has_append_override_fn =
    !std::is_null_pointer_v<decltype(append_override_fn<std::string, T>)>;

// Wrapper for `std::enable_if`, allowing this abbreviated usage:
//    enable_if_append_0<A, T, is_thingy_v<T>> = 0
//
// Semantically, it filters out specializations where the `A` isn't an
// appendable target, or the T has an append override function, or where the
// optional bool is false.
template<typename A, typename T, bool B = true>
using enable_if_append_0 = std::enable_if_t<
    is_append_target_v<A> && !has_append_override_fn<T> && B, bool>;

} // namespace appendplugin
inline namespace appending {

//
// Append, Concat, and Join
//

// The `append`, `append_join`, and `append_join_with` functions take a
// `target`, which can be a `std::string` or any `std::ostream`, as the first
// parameter and append the rest to it.
//
// The `concat`, `join`, and `join_with` functions take the pieces and return
// the whole as a string.
//
// For the `append_join_with` and `join_with` functions, the parameter right
// after `target` is interpreted as the delimiter to separate the other values
// with. The `append_join` and `join` functions instead default the delimiter
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
// Containers include `std::pair`, `std::tuple`, `std::initializer_list`, and
// anything you can do a ranged-for over, such as `std::vector`. For keyed
// containers, such as `std::map`, only the values are used, unless
// `join_opt` specifies otherwise. Containers may be nested arbitrarily.
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

// Append one stringlike thing to `target`.
template<typename A, typename T,
    enable_if_append_0<A, T, is_string_view_convertible_v<T>> = 0>
constexpr auto& append(A& target, const T& part) {
  auto a = make_appender(target);
  if constexpr (is_char_ptr_v<T>)
    a.append(part ? std::string_view{part} : std::string_view{});
  else
    a.append(part);
  return target;
}

// Append one integral number (or `char`) to `target`. When called directly,
// `base`, `width`, and `pad` may be specified.
template<int base = 10, size_t width = 0, char pad = ' ', typename A,
    typename T, enable_if_append_0<A, T, is_integral_number_v<T>> = 0>
constexpr auto& append(A& target, T part) {
  auto radix = base;
  if constexpr (std::is_same_v<T, char>)
    make_appender(target).append(part);
  else
    append_num<base, width, pad>(target, part);
  return target;
}

// Append one floating-point number to `target`. When called directly, `fmt`,
// `precision`, `width`, and `pad` may be specified.
template<std::chars_format fmt = std::chars_format::general,
    int precision = -1, size_t width = 0, char pad = ' ', typename A,
    typename T, enable_if_append_0<A, T, is_floating_number_v<T>> = 0>
constexpr auto& append(A& target, T part) {
  return append_num<fmt, precision, width, pad>(target, part);
}

// Append one pointer or optional value to `target`.
template<typename A, typename T,
    enable_if_append_0<A, T, is_optional_like_v<T>> = 0>
constexpr auto& append(A& target, const T& part) {
  if (part) append(target, *part);
  return target;
}

// Append one void pointer, as hex, to `target`.
template<typename A, typename T,
    enable_if_append_0<A, T, is_void_ptr_v<T>> = 0>
constexpr auto& append(A& target, T part) {
  return append<16>(target, reinterpret_cast<uintptr_t>(part));
}

// Append one `bool`, as `int`, to `target`.
template<typename A, typename T, enable_if_append_0<A, T, is_bool_v<T>> = 0>
constexpr auto& append(A& target, T part) {
  return append(target, static_cast<int>(part));
}

// Append one scoped or unscoped `enum` to `target`.
template<typename A, typename T, enable_if_append_0<A, T, is_enum_v<T>> = 0>
constexpr auto& append(A& target, T part) {
  return append_enum(target, part);
}

// Append one container, as its element values, to `target` without
// delimiters. See `append_join_with` for delimiter support. When called
// directly, `keyed` may be specified.
template<bool keyed = false, typename A, typename T,
    enable_if_append_0<A, T, is_container_v<T> && !is_pair_like_v<T>> = 0>
constexpr auto& append(A& target, const T& parts) {
  for (auto& part : parts) append(target, container_element_v<keyed>(&part));
  return target;
}

// Append pieces to `target` without delimiters. See `append_join_with` for
// delimiter support.
template<typename A, typename Head, typename Middle, typename... Tail,
    enable_if_append_target_0<A> = 0>
constexpr auto& append(A& target, const Head& head, const Middle& middle,
    const Tail&... tail) {
  append(append(target, head), middle);
  if constexpr (sizeof...(tail) != 0) append(target, tail...);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`
// without delimiters. See `append_join_with` for delimiter support.
template<typename A, template<typename...> typename T, typename... Ts,
    enable_if_append_0<A, T<Ts...>, is_tuple_equiv_v<T<Ts...>>> = 0>
constexpr auto& append(A& target, const T<Ts...>& parts) {
  std::apply(
      [&target](const Ts&... parts) {
        if constexpr (sizeof...(parts) != 0) append(target, parts...);
      },
      parts);
  return target;
}

// Append one variant to `target`, as its current type.
template<typename A, typename T, enable_if_append_0<A, T, is_variant_v<T>> = 0>
constexpr auto& append(A& target, const T& part) {
  if (!part.valueless_by_exception()) {
    std::visit([&target](auto&& part) { append(target, part); }, part);
  }
  return target;
}

// Append one streamable object to `target`.
template<typename A, typename T,
    enable_if_append_0<A, T, stream_append_v<T>> = 0>
constexpr auto& append(A& target, const T& part) {
  return append_stream(target, part);
}

// Append one append-overridden object to `target`.
template<typename A, typename T,
    enable_if_append_target_0<A, has_append_override_fn<T>> = 0>
constexpr A& append(A& target, const T& part) {
  return append_override_fn<A, T>(target, part);
}

// Concatenate pieces together into `std::string` without delimiters. See
// `join` and `join_with` for delimiter support.
template<typename Head, typename... Tail>
constexpr [[nodiscard]] auto concat(const Head& head, const Tail&... tail) {
  std::string target;
  append(target, head, tail...);
  return target;
}

} // namespace appending
inline namespace joinoptions {

//
// Join options
//

// Join option bitmask flags.
enum class join_opt {
  // braced - Show braces around containers; the default behavior.
  braced = 0,
  // flat - Avoid showing braces around containers.
  flat = 1,
  // keyed - Show keys in containers, in addition to values.
  keyed = 2,
  // quoted - Show quotes around strings.
  quoted = 4,
  // prefixed - Prefix with the delimiter.
  prefixed = 8
};

} // namespace joinoptions
} // namespace corvid::strings

// Register join_opt as bitmask.
template<>
constexpr size_t corvid::bitmask::bit_count_v<corvid::strings::join_opt> = 4;

namespace corvid::strings {
inline namespace appendjoinplugin {

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
//    template<auto opt = strings::join_opt::braced, char open = 0,
//        char close = 0, typename A>
//    static A& append_join(A& target, strings::delim d, const person& p) {
//      return corvid::strings::append_join_with<opt, '<', '>'>(target, d,
//          p.last, p.first);
//    }
//
//    template<strings::join_opt opt, char open, char close, typename A>
//    constexpr auto strings::append_join_override_fn<opt, open, close, A,
//        person> = person::append_join<opt, open, close, A>;
//
// Note: You will want to specialize `append_override_fn` as well.

// General case to allow specialization.
template<join_opt opt, char open, char close, typename A, typename T>
constexpr auto append_join_override_fn = nullptr;

// Whether an `append_join_override_fn` is specialized on `T`.
template<typename T>
constexpr bool has_append_join_override_fn =
    !std::is_null_pointer_v<decltype(append_join_override_fn<join_opt::braced,
        '(', ')', std::string, T>)>;

// Wrapper for `std::enable_if`, allowing this abbreviated usage:
//    enable_if_append_join_0<A, T, is_thingy_v<T>> = 0
//
// Semantically, it filters out specializations where the `A` isn't an
// appendable target, the T has an append_join override function, or where the
// optional bool is false.
template<typename A, typename T, bool B = true>
using enable_if_append_join_0 = std::enable_if_t<
    is_append_target_v<A> && !has_append_join_override_fn<T> && B, bool>;

} // namespace appendjoinplugin

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

} // namespace decode
inline namespace joining {

//
// Join
//

// Append one piece to `target`, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, typename T,
    enable_if_append_join_0<A, T,
        !is_container_v<T> && !is_variant_v<T> && !is_optional_like_v<T> &&
            !stream_append_v<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  constexpr bool add_braces = decode::braces_v<opt, open, close>;
  constexpr bool add_quotes =
      is_string_view_convertible_v<T> && decode::quoted_v<opt>;
  d.append_if<decode::delimit_v<opt>>(target);

  if constexpr (add_braces) append(target, open);
  if constexpr (add_quotes) append(target, '"');

  append(target, part);

  if constexpr (add_quotes) append(target, '"');
  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one streamable object to `target`, joining with delim.
template<auto opt = join_opt::braced, char open = '{', char close = '}',
    typename A, typename T,
    enable_if_append_join_0<A, T, stream_append_v<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  constexpr bool add_braces = decode::braces_v<opt, open, close>;

  if constexpr (add_braces) append(target, open);
  append_stream(target, part);
  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one pointer or optional value to `target`, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, typename T,
    enable_if_append_join_0<A, T, is_optional_like_v<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  if (part) append_join_with<opt>(target, d, *part);
  return target;
}

// Append one variant to `target`, as its current type, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, typename T, enable_if_append_join_0<A, T, is_variant_v<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  if (!part.valueless_by_exception()) {
    std::visit(
        [&target, &d](auto&& part) { append_join_with<opt>(target, d, part); },
        part);
  }
  return target;
}

// Append one container, as its element values, to `target`, joining with
// `delim`.
template<auto opt = join_opt::braced, char open = '[', char close = ']',
    typename A, typename T,
    enable_if_append_join_0<A, T, is_container_v<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& parts) {
  constexpr bool add_braces = decode::braces_v<opt, open, close>;
  constexpr auto head_opt = decode::head_opt_v<opt>;
  constexpr auto next_opt = decode::next_opt_v<opt>;
  constexpr bool keyed = decode::keyed_v<opt>;

  d.append_if<decode::delimit_v<opt>>(target);
  if constexpr (add_braces) append(target, open);

  if (auto b = std::cbegin(parts), e = std::cend(parts); b != e) {
    append_join_with<head_opt>(target, d, container_element_v<keyed>(b));

    for (++b; b != e; ++b)
      append_join_with<next_opt>(target, d, container_element_v<keyed>(b));
  }

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one `std::tuple` or `std::pair`, as its elements, to `target`,
// joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, template<typename...> typename T, typename... Ts,
    enable_if_append_join_0<A, T<Ts...>,
        is_tuple_equiv_v<T<Ts...>> && !stream_append_v<T<Ts...>>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T<Ts...>& part) {
  constexpr bool is_pair = is_pair_like_v<decltype(part)>;
  constexpr auto next_open = open ? open : (is_pair ? '(' : '{');
  constexpr auto next_close = close ? close : (is_pair ? ')' : '}');

  std::apply(
      [&target, &d](const Ts&... parts) {
        if constexpr (sizeof...(parts) != 0)
          append_join_with<opt, next_open, next_close>(target, d, parts...);
      },
      part);
  return target;
}

namespace details {

// Helper for `append_join_with` parameter pack overload.
template<join_opt opt, char open = 0, char close = 0, typename A,
    typename Head, typename... Tail>
constexpr auto&
ajwh(A& target, delim d, const Head& head, const Tail&... tail) {
  append_join_with<opt>(target, d, head);
  if constexpr (sizeof...(tail) != 0) ajwh<opt>(target, d, tail...);
  return target;
}
} // namespace details

// Append pieces to `target`, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, typename Head, typename... Tail,
    enable_if_append_target_0<A> = 0>
constexpr auto&
append_join_with(A& target, delim d, const Head& head, const Tail&... tail) {
  constexpr bool add_braces = decode::braces_v<opt, open, close>;
  constexpr auto head_opt = decode::head_opt_v<opt>;
  constexpr auto next_opt = decode::next_opt_v<opt>;

  d.append_if<decode::delimit_v<opt>>(target);
  if constexpr (add_braces) append(target, open);

  append_join_with<head_opt>(target, d, head);

  if constexpr (sizeof...(tail) != 0)
    details::ajwh<next_opt>(target, d, tail...);

  if constexpr (add_braces) append(target, close);
  return target;
}

// Append one append-overridden object to `target`, joining with `delim`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, typename T,
    enable_if_append_target_0<A, has_append_join_override_fn<T>> = 0>
constexpr auto& append_join_with(A& target, delim d, const T& part) {
  return append_join_override_fn<opt, open, close, A, T>(target, d, part);
}

// Append pieces to `target`, joining with a comma and space delimiter.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename A, typename Head, typename... Tail,
    enable_if_append_target_0<A> = 0>
constexpr auto& append_join(A& target, const Head& head, const Tail&... tail) {
  constexpr delim d{", "};
  if constexpr (decode::braces_v<opt, open, close>)
    return append_join_with<opt, open, close>(target, d, head, tail...);
  else
    return append_join_with<opt>(target, d, head, tail...);
}

// Join pieces together, with `delim`, into `std::string`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr [[nodiscard]] auto
join_with(delim d, const Head& head, const Tail&... tail) {
  std::string target;
  if constexpr (decode::braces_v<opt, open, close>)
    append_join_with<opt, open, close>(target, d, head, tail...);
  else
    append_join_with<opt>(target, d, head, tail...);
  return target;
}

// Join pieces together, comma-delimited, into `std::string`.
template<auto opt = join_opt::braced, char open = 0, char close = 0,
    typename Head, typename... Tail>
constexpr [[nodiscard]] auto join(const Head& head, const Tail&... tail) {
  std::string target;
  constexpr delim d{", "sv};
  if constexpr (decode::braces_v<opt, open, close>)
    append_join_with<opt, open, close>(target, d, head, tail...);
  else
    append_join_with<opt>(target, d, head, tail...);
  return target;
}

} // namespace joining
inline namespace bracing {

//
// Braces
//

// For braces, the `delim` is interpreted as a pair of characters.

// Trim off matching braces, returning part.
template<typename R = std::string_view>
constexpr [[nodiscard]] auto
trim_braces(std::string_view whole, delim braces = {"[]"}) {
  auto front = braces.front();
  auto back = braces.back();
  if (whole.size() > 1 && whole.front() == front && whole.back() == back) {
    whole.remove_prefix(1);
    whole.remove_suffix(1);
  }
  return R{whole};
}

// Add braces.
constexpr [[nodiscard]] auto
add_braces(std::string_view whole, delim braces = {"[]"}) {
  return concat(braces.front(), whole, braces.back());
}

} // namespace bracing
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
