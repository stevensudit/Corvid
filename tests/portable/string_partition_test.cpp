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

#include <string>

#include "corvid/strings/string_partition.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;
using namespace corvid::strings::partitioning;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Partition

TEST_CASE("Partition", "[StringPartition]") {
  // The common case: split a key from a value.
  if (true) {
    const string_partition p{"key=value", "="};
    CHECK(p.head == "key"sv);
    CHECK(p.sep == "="sv);
    CHECK(p.tail == "value"sv);
  }
  // Only the first separator partitions; the rest stay in the tail.
  if (true) {
    const string_partition p{"a=b=c", "="};
    CHECK(p.head == "a"sv);
    CHECK(p.sep == "="sv);
    CHECK(p.tail == "b=c"sv);
  }
  // Multi-character separator.
  if (true) {
    const string_partition p{"std::string", "::"};
    CHECK(p.head == "std"sv);
    CHECK(p.sep == "::"sv);
    CHECK(p.tail == "string"sv);
  }
  // Separator at the edges leaves an empty head or tail.
  if (true) {
    const string_partition p{"=x", "="};
    CHECK(p.head == ""sv);
    CHECK(p.sep == "="sv);
    CHECK(p.tail == "x"sv);
  }
  if (true) {
    const string_partition p{"x=", "="};
    CHECK(p.head == "x"sv);
    CHECK(p.sep == "="sv);
    CHECK(p.tail == ""sv);
  }
  // Not found: everything lands in the head, and the empty separator view
  // signals the outcome.
  if (true) {
    const string_partition p{"abc", "="};
    CHECK(p.head == "abc"sv);
    CHECK(p.sep.empty());
    CHECK(p.tail.empty());
  }
  // Empty input.
  if (true) {
    const string_partition p{"", "="};
    CHECK(p.head.empty());
    CHECK(p.sep.empty());
    CHECK(p.tail.empty());
  }
  // An empty separator is never found.
  if (true) {
    const string_partition p{"abc", ""};
    CHECK(p.head == "abc"sv);
    CHECK(p.sep.empty());
    CHECK(p.tail.empty());
  }
  // A `std::string` converts transparently.
  if (true) {
    const std::string s{"user@example.com"};
    const string_partition p{s, "@"};
    CHECK(p.head == "user"sv);
    CHECK(p.tail == "example.com"sv);
  }
}

#pragma endregion
#pragma region Rpartition

TEST_CASE("Rpartition", "[StringPartition]") {
  // Split around the last separator instead of the first.
  if (true) {
    const string_rpartition p{"a=b=c", "="};
    CHECK(p.head == "a=b"sv);
    CHECK(p.sep == "="sv);
    CHECK(p.tail == "c"sv);
  }
  // The classic use: peel the extension off a filename.
  if (true) {
    const string_rpartition p{"archive.tar.gz", "."};
    CHECK(p.head == "archive.tar"sv);
    CHECK(p.tail == "gz"sv);
  }
  // Not found: everything lands in the tail, mirroring partition.
  if (true) {
    const string_rpartition p{"abc", "="};
    CHECK(p.head.empty());
    CHECK(p.sep.empty());
    CHECK(p.tail == "abc"sv);
  }
  // Self-overlapping separator: `rfind` locates the last whole match.
  if (true) {
    const string_rpartition p{"aaa", "aa"};
    CHECK(p.head == "a"sv);
    CHECK(p.sep == "aa"sv);
    CHECK(p.tail == ""sv);
  }
  // Both partitions share a base, so one function can accept either result.
  if (true) {
    const auto describe = [](const string_partition_base<>& parts) {
      return std::string{parts.head} + "|" + std::string{parts.tail};
    };
    CHECK(describe(string_partition{"a=b=c", "="}) == "a|b=c");
    CHECK(describe(string_rpartition{"a=b=c", "="}) == "a=b|c");
  }
}

#pragma endregion
#pragma region PartitionAnchoring

TEST_CASE("PartitionAnchoring", "[StringPartition]") {
  // Probe the anchoring contract: the three views tile the input exactly, so
  // their concatenation reconstructs it, even when empty.
  const auto whole = "a=b"sv;
  if (true) {
    const string_partition p{whole, "="};
    CHECK(p.head.data() == whole.data());
    CHECK(p.sep.data() == p.head.data() + p.head.size());
    CHECK(p.tail.data() == p.sep.data() + p.sep.size());
    CHECK(p.head.size() + p.sep.size() + p.tail.size() == whole.size());
  }
  // Not-found empties anchor at the end for partition, at the start for
  // rpartition.
  if (true) {
    const string_partition p{whole, "!"};
    CHECK(p.sep.data() == whole.data() + whole.size());
    CHECK(p.tail.data() == whole.data() + whole.size());
  }
  if (true) {
    const string_rpartition p{whole, "!"};
    CHECK(p.head.data() == whole.data());
    CHECK(p.sep.data() == whole.data());
    CHECK(p.tail.data() == whole.data());
  }
}

#pragma endregion
#pragma region WidePartition

TEST_CASE("WidePartition", "[StringPartition]") {
  // Wide strings come along for free, deduced through CTAD.
  if (true) {
    const string_partition p{L"key=value", L"="};
    CHECK(p.head == L"key"sv);
    CHECK(p.sep == L"="sv);
    CHECK(p.tail == L"value"sv);
  }
  if (true) {
    const string_rpartition p{L"a=b=c", L"="};
    CHECK(p.head == L"a=b"sv);
    CHECK(p.tail == L"c"sv);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
