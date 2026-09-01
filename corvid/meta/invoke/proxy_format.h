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
#include <algorithm>
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
// through the `?` debug request. An empty handle renders "(empty)".
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
// the narrow `std::format_context` (there is no wide bridge). Two unrelated
// `extends` bases that are both formattable collide on the reserved name,
// as sibling same-name methods do; compose them under a shared formattable
// ancestor instead.
using formattable =
    method<"__format", std::format_context::iterator(std::string_view,
                           std::format_context&, bool) const>;

// Concept for a facade that declares the reserved format method, its own or
// inherited, making its handles `std::formattable`.
template<typename F>
concept FormattingFacade =
    Facade<F> && implementation::facade_declares<F, "__format">();

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
                bool debug) -> std::format_context::iterator {
      return corvid::meta::format_with_spec(*static_cast<const T*>(target),
          spec, ctx, debug);
    };
  }
};

// `erased_call` is a pending "__format" call on a handle, packaged as a
// formattable value.
//
// Formatting one through `std::vformat` replays the call under a canonical
// `std::format_context`, which is how `proxy_formatter` serves a context of
// some other type.
template<typename Handle>
struct erased_call {
  const Handle* handle;
  std::string_view spec_tail;
  bool is_debug;
};

// `proxy_formatter` is the one `std::formatter` implementation behind every
// dispatching handle of a `FormattingFacade`.
//
// `parse` keeps the spec tail as text, and at format time the erased
// target's own formatter runs under it (the `format_with_spec` technique).
// The `?` debug request carries through `set_debug_format`, the way the std
// range formatter asks its elements to quote themselves.
//
// An empty handle renders the unquoted marker "(empty)", unpadded, since
// fill and width belong to the spec a present target serves.
template<typename Handle>
struct proxy_formatter {
  constexpr auto parse(std::basic_format_parse_context<char>& ctx) {
    // Keep the tail, not just our own spec, so the target's parse sees the
    // closing '}' the way it would under `std::format`.
    const std::string_view spec{ctx.begin(), ctx.end()};
    spec_tail_ = spec;
    return ctx.begin() +
           static_cast<ptrdiff_t>(corvid::meta::calc_nested_spec_size(spec));
  }

  constexpr void set_debug_format() noexcept { is_debug_ = true; }

  // Format `h` through its target's own formatter.
  //
  // The erased channel is `std::format_context`, which every `std::format`
  // and `format_to` call reaches directly. The declaration stays
  // context-generic because a formatter can be handed another context type:
  // the `std::formattable` probes use a synthetic one, and libc++ formats
  // range and tuple elements through a retargeting one. A foreign context is
  // served by replaying the call under a canonical context and copying the
  // text out, at the cost of a temporary string.
  template<typename FormatContext>
  FormatContext::iterator format(const Handle& h, FormatContext& ctx) const {
    if (!h) {
      auto out = ctx.out();
      for (const char ch : std::string_view{"(empty)"}) *out++ = ch;
      return out;
    }
    if constexpr (std::same_as<FormatContext, std::format_context>) {
      return h.template call<"__format">(spec_tail_, ctx, is_debug_);
    } else {
      const erased_call<Handle> call{&h, spec_tail_, is_debug_};
      const auto text = std::vformat("{}", std::make_format_args(call));
      return std::ranges::copy(text, ctx.out()).out;
    }
  }

private:
  std::string_view spec_tail_;
  bool is_debug_{};
};

} // namespace implementation

}}} // namespace corvid::meta::prox

#pragma region Formatter specializations

// The replay formatter behind `erased_call`.
//
// It is only ever reached at the top of a `std::vformat`, so it takes the
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
    return call.handle->template call<"__format">(call.spec_tail, ctx,
        call.is_debug);
  }
};

// One-line `std::formatter` specializations, deriving the shared base, one
// per dispatching handle flavor. The weak proxies stay out, mirroring their
// lack of `call`.

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
