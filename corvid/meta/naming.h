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
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>

// For demangling in `type_name`.
#ifndef _MSC_VER
#include <cxxabi.h>
#endif

namespace corvid { inline namespace meta { inline namespace naming {

#pragma region type_name

// Extract fully qualified type name.
//
// This is a crude solution, but sufficient for debugging.
template<typename T>
std::string type_name() {
  using TR = std::remove_reference_t<T>;
  std::unique_ptr<char, void (*)(void*)> own{
#ifndef _MSC_VER
      abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
#else
      nullptr,
#endif
      [](void* ptr) { std::free(ptr); }};
  std::string r{own ? own.get() : typeid(TR).name()};
  if (std::is_const_v<TR>) r += " const";
  if (std::is_volatile_v<TR>) r += " volatile";
  if (std::is_lvalue_reference_v<T>)
    r += "&";
  else if (std::is_rvalue_reference_v<T>)
    r += "&&";
  return r;
}

// Extract fully qualified type name, deducing it from the parameter.
template<typename T>
std::string type_name(T&&) {
  return type_name<T>();
}

// Extract a type name cleaned up for humans (and generated code).
//
// Builds on `type_name`, stripping elaborated-type keywords, pointer-width
// and calling-convention annotations (MSVC spells all of these),
// inline-namespace segments, and anonymous-namespace prefixes, normalizing
// spacing, collapsing the expanded `std::string` spelling, and restoring
// top-level cv in the leading position, except trailing on a pointer
// (member pointers included), where a leading const would name a different
// type. Best-effort, like
// `type_name`; this band cannot use the string utilities, so the edits are
// hand-rolled.
template<typename T>
std::string friendly_type_name() {
  using TR = std::remove_reference_t<T>;
  auto r = type_name<std::remove_cv_t<TR>>();
  auto replace = [&r](std::string_view what, std::string_view with) {
    for (auto pos = 0UZ; (pos = r.find(what, pos)) != std::string::npos;
        pos += with.size())
      r.replace(pos, what.size(), with);
  };
  replace("class ", "");
  replace("struct ", "");
  replace("enum ", "");
  replace("__1::", "");
  replace("__cxx11::", "");
  replace("(anonymous namespace)::", "");
  replace("`anonymous namespace'::", "");
  replace(" __ptr64", "");
  replace("__cdecl", "");
  // Collapse spaced closing brackets to a fixpoint: one pass cannot see the
  // pair a replacement just created ("> > >" needs two). A blanket "> " to
  // ">" replacement would handle any depth in one pass but is wrong, because
  // that spelling also occurs before east const ("std::vector<int> const*").
  while (r.contains("> >")) replace("> >", ">>");
  replace(", ", ",");
  replace(
      "std::basic_string<char,std::char_traits<char>,"
      "std::allocator<char>>",
      "std::string");
  replace(",", ", ");
  if (std::is_pointer_v<TR> || std::is_member_pointer_v<TR>) {
    if (std::is_const_v<TR>) r += " const";
    if (std::is_volatile_v<TR>) r += " volatile";
  } else {
    if (std::is_volatile_v<TR>) r = "volatile " + r;
    if (std::is_const_v<TR>) r = "const " + r;
  }
  if (std::is_lvalue_reference_v<T>)
    r += "&";
  else if (std::is_rvalue_reference_v<T>)
    r += "&&";
  return r;
}

#pragma endregion
}}} // namespace corvid::meta::naming
