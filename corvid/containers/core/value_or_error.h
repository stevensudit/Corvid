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

#pragma once
#include <cassert>
#include <concepts>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "../../meta/fixed_string.h"
#include "optional_ptr.h"

namespace corvid { inline namespace container {
inline namespace value_or_errors {

#pragma region Documentation

// Overview and tutorial:
//
// A `value_or_error` is a result that contains either the value or the error
// that explains its absence. It is parallel to a `std::optional`, except that,
// when there's no value, it carries an error instead of being empty.
//
// If this sounds a lot like `std::expected`, that's because this class is a
// deliberately reduced alternative to it: an improper subset, and not shy
// about it.
//
// It was not designed by a committee, much less the result of trying
// to unify any number of extant alternatives. As a result, it's simpler and
// more reliable, hence more useful in practice. The motto here is: expect
// less, receive more. In contrast, the standard library's version is a
// compromise on a compromise on a compromise.
//
// This design benefits from being firm and opinionated. By requiring that the
// value and error types be nothrow-movable, distinct from each other, and not
// implicitly convertible to each other, it gets rid of the `std::unexpected`
// requirement and the wide construction surface. Fundamentally, a `T` is a
// value, an `E` is an error, and each converts directly to the result.
//
// Enough editorializing and handwaving. This is what it looks like in action:
//
//   using door_result = value_or_error<std::string, rejection_reason>;
//[...]
//   door_result open_door() {
//     if (door_.is_locked()) return rejection_reason::great_failure;
//     return "great success!"s; // Note the suffix; more on that later.
//[...]
//    auto r = open_door();
//    if (r) std::cout << "Opened with " << *r << '\n';
//    if (!r) std::cout << "Failed: " << r.as_error() << '\n';
//
// ---
//
// The error type here is an `enum` (which was registered as a `sequence_enum`
// so as to make that streaming work), but it could also be any `struct` with
// any number of fields. It could be a class with private members as well,
// although that's not a great choice for an error.
//
// If you wanted to return a `std::string` as the error, even though that's
// also the type for the value, you can still do it, but not directly. Instead,
// you would wrap the error string in a `struct` to make it a distinct type
// (which has the desirable side-effect of making the entire result a distinct
// type).
//
// While you could do this manually, it would be easier to use the
// `error_value` helper:
//
//   using rejection_text = error_value<struct RejectionTag>;
//   using door_result = value_or_error<std::string, rejection_text>;
//[...]
//   door_result open_door() {
//     if (door_.is_locked()) return rejection_text{"great failure."s};
//     return "great success!"s;
//
// In this case, because we chose a `std::string` as both the value and the
// contents of the error, we need to explicitly construct a `rejection_text`.
//
// Where `std::expected` requires you to wrap the error in a `std::unexpected`
// in all cases, this class requires invoking the constructor by name, but only
// when there would otherwise be an ambiguity. And, of course, you get to
// choose the types, which lets you avoid ambiguous pairs if you prefer.
//
// Any ambiguity will result in a clean compile error.
//
// ---
//
// Either type being specialized on could be a reference. This has all the
// standard advantages and risks, in that it's lightweight but not owning. So,
// for example:
//
//   using lookup_result = value_or_error<const std::string&, failure_reason>;
//[...]
//   lookup_result find_name(std::string_view name) {
//     if (const auto found = table_.find(name)) return *found;
//     return failure_reason{"not found"};
//
// In this example, you could argue for returning a `std::string_view` instead,
// although that also has its disadvantages. You can also specialize on a raw
// or smart pointer.
//
// ---
//
// Construction and assignment are intentionally narrow, with automatic
// conversions avoided. As a result, you may need to be more explicit about the
// value. Using the earlier definitions:
//
//   door_result r{}; // Default to error text.
//   r = "certain success."s; // compiles
//   r = "certain success."; // does not compile; won't convert twice
//   r = {"certain success."}; // compiles
//   r = rejection_text{"certain failure."}; // compiles
//   r = door_result::error_type{"certain failure."}; // compiles
//   r = {{"certain failure."}}; // does not compile; what type even is this?
//   r = {{.reason = "certain failure."}}; // compiles
//
// The last line works because `std::string` can't be initialized like that,
// but if you chose a `T` that could, then it would no longer compile.
//
// ---
//
// When a function calls another that returns a `value_or_error` with the same
// specializations as its own result type, it can return this directly.
// However, it's common for the specializations to overlap on only one wing:
// just the value or just the error.
//
// It is always the case that you need to check what a `value_or_error`
// contains before dereferencing it. So when returning a result that only
// matches one wing, you have to do the same.
//
// However, when you're just returning it and not accessing its contents, you
// don't need to dereference it. Instead, a result converts across
// specializations along whichever wing they share, so long as its current
// contents are of the type they share.
//
// This means that a successful `value_or_error<int, excuse_id>`, which
// contains an `int`, converts to a successful `value_or_error<int,
// failure_reason>` by copying that `int` into it, even though the error types,
// `excuse_id` and `failure_reason`, are different.
//
// Likewise, a failed `value_or_error<long, failure_reason>`, which contains a
// `failure_reason`, converts to a failed `value_or_error<Point,
// failure_reason>` by copying that `failure_reason` into it, even though
// `long` and `Point` are different.
//
// Note that whether the conversion works depends on what value it contains,
// and that can only be determined at runtime. This is why, even with this
// convenience syntax, it is still your job to check if the contents of the
// `value_or_error` are of the correct type before returning them.
//
// Failure to do so is caught by an assert in debug builds and is UB in release
// builds, not a compile error or even a clean exception. Having said that, if
// you stick to the pattern of checking before returning, it will always work
// as expected.
//
//   value_or_error<int, failure_reason> get_int_or_reason() {
//     // Implicitly returns the `int`.
//     if (auto r = get_int_or_excuse()) return r;
//     // Explicitly returns the `int` by first dereferencing.
//     if (auto r = get_int_or_excuse()) return *r;
//[...]
//   value_or_error<Point, failure_reason> get_point_or_reason() {
//     // Implicitly returns the `failure_reason`.
//     if (auto s = get_long_or_failure_reason(); !s) return s;
//     // Explicitly returns the `failure_reason` by first dereferencing.
//     if (auto s = get_long_or_failure_reason(); !s) return s.as_error();
//
// The second instance of each example does the same thing as the first, but
// with an unnecessarily explicit dereference. It is not any safer or more
// efficient, just more verbose, so it's shown here only for comparison.
//
// ---
//
// Quite intentionally, when copying the content between instances with
// overlapping specializations, no conversion is applied to either wing. The
// shared wing is copied as is, and a differing wing never converts, even when
// its types are convertible.
//
// Allowing such conversions would create the ambiguities that force the class
// down the path of requiring a `std::unexpected` wrapper for all error types
// in all cases. This means that if what's being returned is not an exact type
// match, you will need to dereference it yourself and possibly convert it.
//
//   value_or_error<long, excuse_id> get_long_or_excuse() {
//     // The next line works because the `int` widens to `long`.
//     // Here, the dereference is load-bearing, not verbosity.
//     if (auto t = get_int_or_reason()) return *t;
//     // The next line fails, because it won't convert results directly.
//     if (auto t = get_int_or_reason()) return t;
//
// In the second case of the example above, it will not extract the `int` from
// the `value_or_error<int, failure_reason>` and copy it into a
// `value_or_error<long, excuse_id>`, even though `int` implicitly widens to
// `long`.
//
// ---
//
// The standard pattern is to evaluate the `value_or_error` to determine if it
// has a value, before accessing it. For example:
//
//     if (auto r = get_int_or_reason()) got_int = *r;
//
// You can also use optional access with `maybe_value` and `maybe_error`. These
// return an `optional_ptr`, which is akin to `std::optional` but contains a
// pointer instead of a copy.
//
// Its adapters (`value_or`, `value_or_fn`, and the rest) provide a monadic
// interface so this class doesn't have to. (Less is more.)
//
// The primary reason to use optional access is to enable those monadic
// adapters, but it also provides somewhat more uniform handling of the two
// cases:
//
//    auto r = open_door();
//    // A wild monad appears.
//    log(r.maybe_value().value_or("nope"s));
//    // As a reminder, the usual way looks like this.
//    if (r) pass_on_success(*r);
//    // And the usual failure path looks like this.
//    if (!r) log_error(r.as_error());
//    // The success path doesn't look much different, just an extra step.
//    if (auto s = r.maybe_value()) return pass_on_success(*s);
//    // The failure path is perhaps a bit cleaner.
//    if (auto e = r.maybe_error()) return log_error(*e);
//
// So the takeaway here is that the feature exists and you can use it if it
// helps, but it mostly helps with the monadic adapters.

#pragma endregion
#pragma region value_or_error

// `value_or_error` holds either a value `T` or an error `E`, discriminated.
//
// The enforced contract: `T` and `E` are object types or lvalue references,
// neither implicitly convertible to the other (which also rules out `T` being
// `E`), and nothrow-movable as stored. Construction from `T` or `E` is
// unambiguous and moves cannot fail, so there is no valueless state to reason
// about. Assignment converts exactly as construction does.
//
// Only implicit conversion is barred, which leaves explicit construction
// untouched. An `E` that wraps a `T`, such as `error_value<Tag>` wrapping a
// `std::string` `reason`, satisfies the contract because `E{t}` is
// direct-initialization rather than a conversion. That is the point of
// spelling an error by naming its type.
//
// It can be specialized on a const reference type but not a const
// non-reference type. This ensures that the object remains mutable.
//
// What the narrowness costs is deliberate, not an oversight:
// - No construction from a type merely convertible to `T` or `E`. One type
//   convertible to both would be ambiguous, so the conversion is spelled at
//   the call site instead. In particular, a bare string literal is a `char[]`,
//   not a `std::string`, so write `"reason"s`.
// - Access is a narrow contract: check before reaching in. Failing to check
//   is a logic error in the caller, so it is asserted, not converted into a
//   runtime error.
// - No throwing `value` accessor: the whole point is to signal failure more
//   gently than a throw, and a throwing accessor would reward skipping the
//   check. A caller who wants `if (!r) throw x;` can write exactly that.
// - No `emplace` and no mutable access: a result is a lightweight,
//   fire-and-forget return value. To "change" its contents, assign a fresh
//   one.
//
// Access follows the model of `std::optional`, with `operator*` and
// `operator->` to reach the value, and `as_error` to get the error, each
// asserting its presence but intentionally not throwing.
//
// The `maybe_value` and `maybe_error` methods return an `optional_ptr` that is
// empty when the other alternative is engaged, allowing you to do the check
// and then just dereference.
//
// Distinctness between `value_or_error` result types comes directly from the
// distinctness of the `T` and `E` types they're specialized on. Typically, `E`
// is custom to a particular function, while `T` is a well-known value type,
// such as `std::string`. However, it's just as possible, if perhaps less
// common, to have a few result types share the `E`, such as an enum, while
// using whichever `T` is appropriate to that function.
//
// It works logically in any combination because propagation follows the
// overlap. An error travels to any result with the same `E`, a value to any
// result with the same `T`, and specializations sharing neither have no common
// ground that would allow conversion.
//
// To keep two same-shaped results apart, give them distinct error types;
// the optional `error_value` utility class, below, makes that a one-liner, but
// you aren't limited to it.
//
// You can use references or pointers for either wing: it just works. Of
// course, the referent must outlive the result, so construction from a
// temporary (including one materialized by conversion) is deleted. The stored
// form is a `std::reference_wrapper`, but the accessors return the reference
// itself, without a `get` ceremony. The `value_type` and `error_type`
// report the stored forms.
//
// A value-less result (did it work, and if not, why not) is spelled
// `value_or_error<void, E>`. The `void` wing is stored as a `std::monostate`,
// so success is returned as `std::monostate{}`. A `void` error works the same
// way, for a failure that needs no explanation.
template<typename T, typename E>
class [[nodiscard]] value_or_error final {
public:
#pragma region Types

  // Map away the `void` shorthand: a `void` wing is a `std::monostate` wing.
  template<typename X>
  using nonvoid_t = std::conditional_t<std::is_void_v<X>, std::monostate, X>;

  // Map a declared type to its stored form: a reference is stored as a
  // `std::reference_wrapper`, and `void` as a `std::monostate`.
  template<typename X>
  using stored_t = std::conditional_t<std::is_reference_v<X>,
      std::reference_wrapper<std::remove_reference_t<X>>, nonvoid_t<X>>;

  using value_type = stored_t<T>;
  using error_type = stored_t<E>;

  static_assert(
      (std::is_object_v<T> || std::is_lvalue_reference_v<T> ||
          std::is_void_v<T>) &&
          (std::is_object_v<E> || std::is_lvalue_reference_v<E> ||
              std::is_void_v<E>),
      "T and E must be object types, lvalue references, or void");
  static_assert(!std::is_const_v<T> && !std::is_volatile_v<T> &&
                    !std::is_const_v<E> && !std::is_volatile_v<E>,
      "T and E must not be cv-qualified: access is already read-only");
  static_assert(!std::is_convertible_v<nonvoid_t<T>, nonvoid_t<E>> &&
                    !std::is_convertible_v<nonvoid_t<E>, nonvoid_t<T>>,
      "T and E must be distinct and not implicitly convertible to each other");
  static_assert(std::is_nothrow_move_constructible_v<stored_t<T>> &&
                    std::is_nothrow_move_constructible_v<stored_t<E>>,
      "T and E must be nothrow-movable as stored");

private:
  // Access and intake spellings per wing. A reference wing hands back the
  // reference itself; `*_take` is the sink parameter, which for a reference
  // wing is the rvalue overload that gets deleted.
  using value_ref =
      std::conditional_t<std::is_reference_v<T>, T, const nonvoid_t<T>&>;
  using value_move =
      std::conditional_t<std::is_reference_v<T>, T, nonvoid_t<T>&&>;
  using value_take = std::conditional_t<std::is_reference_v<T>,
      std::remove_reference_t<nonvoid_t<T>>&&, nonvoid_t<T>&&>;
  using value_ptr = std::add_pointer_t<std::remove_reference_t<value_ref>>;

  using error_ref =
      std::conditional_t<std::is_reference_v<E>, E, const nonvoid_t<E>&>;
  using error_move =
      std::conditional_t<std::is_reference_v<E>, E, nonvoid_t<E>&&>;
  using error_take = std::conditional_t<std::is_reference_v<E>,
      std::remove_reference_t<nonvoid_t<E>>&&, nonvoid_t<E>&&>;
  using error_ptr = std::add_pointer_t<std::remove_reference_t<error_ref>>;

#pragma endregion
#pragma region Construction
public:
  // Default is an error holding a value-initialized `E`.
  //
  // A result made from nothing has no value to claim, so `return {};` reports
  // a generic failure. (`std::expected` defaults to a value instead; we
  // consider that a phantom success.)
  constexpr value_or_error() noexcept(
      std::is_nothrow_default_constructible_v<stored_t<E>>)
  requires(std::is_default_constructible_v<stored_t<E>>)
      : v_{std::in_place_type<error_type>} {}

  // Implicit conversion from `T` and `E`.
  //
  // A `T` is a value; an `E` is an error. A reference `T` or `E` binds an
  // lvalue and stores the binding, not a copy; there its rvalue overload is
  // deleted, and since a conversion result is a prvalue, that also rejects a
  // temporary materialized by conversion.
  constexpr value_or_error(value_ref v) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<T>>)
      : v_{std::in_place_type<value_type>, v} {}

  constexpr value_or_error(value_take v) noexcept
  requires(!std::is_reference_v<T>)
      : v_{std::in_place_type<value_type>, std::move(v)} {}

  value_or_error(value_take)
  requires(std::is_reference_v<T>)
  = delete;

  constexpr value_or_error(error_ref e) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<E>>)
      : v_{std::in_place_type<error_type>, e} {}

  constexpr value_or_error(error_take e) noexcept
  requires(!std::is_reference_v<E>)
      : v_{std::in_place_type<error_type>, std::move(e)} {}

  value_or_error(error_take)
  requires(std::is_reference_v<E>)
  = delete;

  // Implicit conversion of error from compatible `value_or_error`.
  //
  // Precondition: `other` holds an error, not a value.
  template<typename U>
  requires(!std::same_as<U, T>)
  constexpr value_or_error(const value_or_error<U, E>& other) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<E>>)
      : v_{std::in_place_type<error_type>, other.as_error()} {}

  template<typename U>
  requires(!std::same_as<U, T>)
  constexpr value_or_error(value_or_error<U, E>&& other) noexcept
      : v_{std::in_place_type<error_type>, std::move(other).as_error()} {}

  // Implicit conversion of value from compatible `value_or_error`.
  //
  // Precondition: `other` holds a value, not an error.
  template<typename F>
  requires(!std::same_as<F, E>)
  constexpr value_or_error(const value_or_error<T, F>& other) noexcept(
      std::is_nothrow_copy_constructible_v<stored_t<T>>)
      : v_{std::in_place_type<value_type>, *other} {}

  template<typename F>
  requires(!std::same_as<F, E>)
  constexpr value_or_error(value_or_error<T, F>&& other) noexcept
      : v_{std::in_place_type<value_type>, *std::move(other)} {}

#pragma endregion
#pragma region Access
private:
  // Return the stored pointer for the `X` wing, unwrapping a reference wing,
  // or null when the other wing is engaged. Constness follows `self` for an
  // object wing and the referent for a reference wing.
  template<typename X>
  constexpr auto do_get_ptr(this auto&& self) noexcept {
    auto p = std::get_if<stored_t<X>>(&self.v_);
    if constexpr (std::is_reference_v<X>)
      return p ? &p->get() : nullptr;
    else
      return p;
  }

public:
  // Whether there is a value rather than an error.
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<value_type>(v_);
  }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  // Access the value, asserting its presence.
  [[nodiscard]] constexpr value_ref operator*() const& {
    assert(has_value());
    return *do_get_ptr<T>();
  }
  [[nodiscard]] constexpr value_move operator*() && {
    assert(has_value());
    return static_cast<value_move>(*do_get_ptr<T>());
  }
  [[nodiscard]] constexpr value_ptr operator->() const {
    assert(has_value());
    return do_get_ptr<T>();
  }

  // Access the error, asserting its presence.
  [[nodiscard]] constexpr error_ref as_error() const& {
    assert(!has_value());
    return *do_get_ptr<E>();
  }
  [[nodiscard]] constexpr error_move as_error() && {
    assert(!has_value());
    return static_cast<error_move>(*do_get_ptr<E>());
  }

  // The value, or empty if this is an error.
  [[nodiscard]] constexpr optional_ptr<value_ptr>
  maybe_value() const noexcept {
    return do_get_ptr<T>();
  }

  // The error, or empty if this is a value.
  [[nodiscard]] constexpr optional_ptr<error_ptr>
  maybe_error() const noexcept {
    return do_get_ptr<E>();
  }

#pragma endregion
#pragma region Data members
private:
  std::variant<stored_t<T>, stored_t<E>> v_;

#pragma endregion
};

#pragma endregion
#pragma region error_value

// Strongly typed error reason.
//
// Companion for making one error type distinct from another. `Tag` gives the
// wrapped reason a nominal identity, in the manner of `strong_type`, and it
// is mandatory because distinctness is the whole point.
//
// The type of the reason defaults to `std::string`, and `Default` supplies the
// reason's initial value, so the minimal spelling names only the domain,
// through that tag, and default-constructs to "Unknown error".
//
// This composes with `value_or_error`'s own default constructor so that
// `return {};` is a self-describing generic failure. A domain with its own
// reason type passes its own default, such as an enum value.
//
// Spell an error by naming its type: `parse_error{"bad digit"}`. The anonymous
// `return {{"bad digit"}};` spelling is an anti-pattern: it resolves by
// conversion rank against both wings, not by intent. When both wings can
// absorb the inner list, it is ambiguous and fails to compile; when only `T`
// can absorb it, or the inner element is already a `T`, it silently constructs
// a value instead of an error.
//
// Staying an aggregate is load-bearing, not incidental. A non-explicit
// constructor from the reason type would make the error implicitly convertible
// from it, which is exactly what `value_or_error` forbids, so every
// specialization pairing this error with that reason type would stop
// compiling. Any constructor added here needs to be `explicit`.
//
//   using parse_error = error_value<struct ParseTag>;
//   using io_error = error_value<struct IoTag, io_errc, io_errc::unknown>;
template<typename Tag, typename E = std::string,
    auto Default = basic_fixed_string{"Unknown error"}>
struct error_value final {
  static_assert(
      requires { E{Default}; }, "Default must be able to initialize E");
  E reason{Default};
};

#pragma endregion

}}} // namespace corvid::container::value_or_errors
