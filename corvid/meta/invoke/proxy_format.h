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
#include <concepts>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "../formatting.h"
#include "proxy_common.h"

// The `std::formatter` bridge for proxy handles.
//
// Opting a facade in with the `formattable` entry makes every dispatching
// handle of that facade formattable, so erased values drop into `std::format`
// like the concrete targets they hide:
//
//    std::format("{}: {:>8}", label, handle);
//
// The full spec grammar of the TARGET applies, because the handle forwards
// the spec to the target's own `std::formatter` at format time. Inside
// ranges and maps, handles quote themselves exactly as their targets would,
// through the `?` debug request. An empty handle renders "(empty)", padded
// as the spec asks.
//
// Include this header where formatting facades are declared; the umbrella
// "proxy.h" leaves it out so that `<format>` stays opt-in.

namespace corvid { inline namespace meta { namespace prox {

// `formattable` is the facade entry that opts the facade into `std::format`.
//
// List it among the facade's entries:
//
//    struct poet
//        : facade<name<"poet">, formattable,
//              method<"recite", std::string() const>> {};
//
// Handles of the facade (the owning proxy, the views, and the shared
// proxies) then satisfy `std::formattable`, and formatting one runs the
// target's own `std::formatter` under the caller's spec. Facades that extend
// a formattable one inherit the ability as they do any method.
//
// Under the hood, this is an ordinary facade method under the reserved name
// "__format", dispatched through the table like any other. Registration is
// constrained accordingly: a target must itself be `std::formattable`, or
// its registration must bind "__format", which is also how a target
// substitutes custom erased formatting for its `std::formatter`. Such a
// binding receives the spec tail (running through the field's closing '}'),
// the erased output context, and whether `?` debug formatting was requested,
// and returns the output iterator; `format_with_spec` is the normal way to
// honor the spec.
//
// The bridge is erased, so compile-time spec checking stops at the handle:
// the target's grammar is enforced at format time, and the erased channel is
// the narrow `std::format_context` (there is no wide bridge). A dynamic width
// or precision is resolved by the bridge and reaches the target as a literal,
// which assumes the standard spec grammar up to the precision.
//
// Two unrelated `extends` bases that are both formattable collide on the
// reserved name, as sibling same-name methods do; compose them under a shared
// formattable ancestor instead.
using formattable = method<implementation::format_method_name,
    std::format_context::iterator(std::string_view, std::format_context&, bool)
        const>;

// Concept for a facade that declares the reserved format method, its own or
// inherited, making its handles `std::formattable`.
template<typename F>
concept FormattingFacade =
    Facade<F> &&
    implementation::facade_declares<F, implementation::format_method_name>();

namespace implementation {

// `format_binding` is the library binding behind the reserved format method.
//
// `make_thunk` falls back to it for a pair whose impl does not bind
// "__format". The minted thunk runs the target's own `std::formatter` under
// the caller's spec, through `format_with_spec`.
template<typename T>
struct format_binding {
  // The thunk exists exactly when `T` formats, which is what gates both the
  // conformance fallback and the table build.
  static consteval auto thunk() noexcept
  requires std::formattable<T, char>
  {
    return +[](const void* target, std::string_view spec,
                std::format_context& ctx,
                bool is_debug) -> std::format_context::iterator {
      return corvid::meta::format_with_spec(*static_cast<const T*>(target),
          spec, ctx, is_debug);
    };
  }
};

// `erased_call` is a pending "__format" call on a handle, packaged as a
// formattable value.
//
// Formatting one through `std::vformat_to` replays the call under a canonical
// `std::format_context`, which is how `proxy_formatter` serves a context of
// some other type.
template<typename Handle>
struct erased_call {
  const Handle* handle{};
  std::string_view spec_tail;
  bool is_debug{};
};

// `proxy_formatter` is the one `std::formatter` implementation behind every
// dispatching handle of a `FormattingFacade`.
//
// `parse` keeps the spec tail as text, and at format time the erased
// target's own formatter runs under it (the `format_with_spec` technique).
// The `?` debug request carries through `set_debug_format`, the way the std
// range formatter asks its elements to quote themselves.
//
// A dynamic width or precision is the bridge's to resolve, because the target
// parses the spec at format time, when an auto `{}` can no longer claim its
// arg id, and a replayed call (see `dispatch`) cannot reach the caller's args
// at all. So `parse` reads the standard grammar with the band's
// `spec_parser` and claims any auto ids, and `format` re-presents the spec to
// the target with the resolved values spelled as literals (the
// `nullable_formatter` technique). A spec with no dynamic field reaches the
// target untouched, so a target grammar that departs from the standard one
// still works there.
//
// An empty handle renders the unquoted marker "(empty)" through the bridge's
// own padding, so fill, align, width, and precision apply to it as they
// would to a present target.
template<typename Handle>
struct proxy_formatter {
  constexpr auto parse(std::basic_format_parse_context<char>& ctx) {
    // Keep the tail, not just our own spec, so the target's parse sees the
    // closing '}' the way it would under `std::format`.
    const std::string_view spec{ctx.begin(), ctx.end()};
    spec_tail_ = spec;
    spec_size_ = corvid::meta::calc_nested_spec_size(spec);
    (void)spec_.parse(spec.substr(0, spec_size_));
    spec_.width_arg.claim_next_automatic(ctx);
    spec_.precision_arg.claim_next_automatic(ctx);
    return ctx.begin() + static_cast<ptrdiff_t>(spec_size_);
  }

  constexpr void set_debug_format() noexcept { spec_.debug = true; }

  // Format `h` through its target's own formatter.
  template<typename FormatContext>
  FormatContext::iterator format(const Handle& h, FormatContext& ctx) const {
    const auto width = spec_.resolve_width(ctx);
    const auto precision = spec_.resolve_precision(ctx);
    if (!h) {
      std::string_view marker{"(empty)"};
      if (precision) marker = marker.substr(0, *precision);
      return spec_.write_padded(ctx.out(), marker, width);
    }
    if (!spec_.is_dynamic()) return dispatch(h, spec_tail_, ctx);
    auto fixed = spec_.rewrite_spec_as_fixed(spec_tail_.substr(0, spec_size_),
        width, precision);
    fixed.push_back('}');
    return dispatch(h, fixed, ctx);
  }

private:
  // Dispatch the call under `spec_tail`.
  //
  // The erased channel is `std::format_context`, which every `std::format`
  // and `format_to` call reaches directly. The declaration stays
  // context-generic because a formatter can be handed another context type:
  // the `std::formattable` probes use a synthetic one, and libc++ formats
  // range and tuple elements through a retargeting one. A foreign context is
  // served by replaying the call under a canonical context, through
  // `std::vformat_to` on its own output iterator and locale.
  template<typename FormatContext>
  FormatContext::iterator dispatch(const Handle& h, std::string_view spec_tail,
      FormatContext& ctx) const {
    if constexpr (std::same_as<FormatContext, std::format_context>) {
      return h.template call<format_method_name>(spec_tail, ctx, spec_.debug);
    } else {
      const erased_call<Handle> call{&h, spec_tail, spec_.debug};
      return std::vformat_to(ctx.out(), ctx.locale(), "{}",
          std::make_format_args(call));
    }
  }

  std::string_view spec_tail_;
  size_t spec_size_{};
  corvid::meta::spec_parser<char> spec_;
};

} // namespace implementation

}}} // namespace corvid::meta::prox

#pragma region Formatter specializations

// The replay formatter behind `erased_call`.
//
// It is only ever reached at the top of a `std::vformat_to`, so it takes the
// canonical context outright.
template<typename Handle>
struct std::formatter<corvid::meta::prox::implementation::erased_call<Handle>,
    char> {
  constexpr auto parse(std::basic_format_parse_context<char>& ctx) {
    return ctx.begin();
  }

  std::format_context::iterator
  format(const corvid::meta::prox::implementation::erased_call<Handle>& call,
      std::format_context& ctx) const {
    return call.handle->template call<
        corvid::meta::prox::implementation::format_method_name>(call.spec_tail,
        ctx, call.is_debug);
  }
};

// One-line `std::formatter` specializations, deriving the shared base, one
// per dispatching handle flavor. The weak proxies stay out, mirroring their
// lack of `call`.
//
// They are spelled out rather than collapsed into one bare-`H`
// specialization constrained on `handle_facade<H>`. A program-defined
// specialization of a std template must depend on a program-defined type,
// and with a bare pattern that dependence lives only in the constraint,
// which the standard libraries police differently and which has to stay
// unambiguous against their own bare-pattern formatters (ranges, pointers).
// The collapsed form would work on some legs at a given time; the five lines
// work everywhere.

template<corvid::meta::prox::FormattingFacade F>
struct std::formatter<corvid::meta::prox::proxy_view<F>, char>
    : corvid::meta::prox::implementation::proxy_formatter<
          corvid::meta::prox::proxy_view<F>> {};

template<corvid::meta::prox::FormattingFacade F>
struct std::formatter<corvid::meta::prox::const_proxy_view<F>, char>
    : corvid::meta::prox::implementation::proxy_formatter<
          corvid::meta::prox::const_proxy_view<F>> {};

template<corvid::meta::prox::FormattingFacade F,
    corvid::meta::invocable_policy Policy>
struct std::formatter<corvid::meta::prox::proxy<F, Policy>, char>
    : corvid::meta::prox::implementation::proxy_formatter<
          corvid::meta::prox::proxy<F, Policy>> {};

template<corvid::meta::prox::FormattingFacade F>
struct std::formatter<corvid::meta::prox::shared_proxy<F>, char>
    : corvid::meta::prox::implementation::proxy_formatter<
          corvid::meta::prox::shared_proxy<F>> {};

template<corvid::meta::prox::FormattingFacade F>
struct std::formatter<corvid::meta::prox::const_shared_proxy<F>, char>
    : corvid::meta::prox::implementation::proxy_formatter<
          corvid::meta::prox::const_shared_proxy<F>> {};

#pragma endregion
