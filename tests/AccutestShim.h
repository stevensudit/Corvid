#include <iostream>
#include <sstream>

// From: https://github.com/mity/acutest
#include "../../acutest/include/acutest.h"

namespace accutest_shim {

template<typename T>
concept OStreamable = requires(T t, std::ostream& os) { os << t; };

auto inline stream_to_text(const auto& v) {
  std::ostringstream os;
  using V = std::remove_cvref_t<decltype(v)>;
  if constexpr (!OStreamable<decltype(v)>) {
    for (const auto& e : v) {
      if (!os.str().empty()) os << ", ";
      os << e;
    }
  } else if constexpr (std::is_void_v<V> || std::is_same_v<V, const void*>) {
    os << "{}";
  } else if constexpr (std::is_pointer_v<V>) {
    if (v)
      os << *v;
    else
      os << "{}";
  } else {
    os << v;
  }
  return os.str();
}

} // namespace accutest_shim

#define VALUE_MSG(actual, expected)                                           \
  TEST_MSG("Actual:   `%s`", accutest_shim::stream_to_text(actual).c_str());  \
  TEST_MSG("Expected: `%s`",                                                  \
      accutest_shim::stream_to_text(expected).c_str());                       \
  TEST_MSG("File: %s Line: %d Function: %s", __FILE__, __LINE__, __FUNCTION__);

#define EXPECT_EQ(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ == expected_), "%s",                                 \
        ("(" #actual " == " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_EQ(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_((actual_ == expected_), "%s",                                \
        ("(" #actual " == " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define EXPECT_NE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ != expected_), "%s",                                 \
        ("(" #actual " != " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_NE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_((actual_ != expected_), "%s",                                \
        ("(" #actual " != " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define EXPECT_LT(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ < expected_), "%s",                                  \
        ("(" #actual " < " #expected ")"));                                   \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_LT(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_((actual_ < expected_), "%s",                                 \
        ("(" #actual " < " #expected ")"));                                   \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define EXPECT_LE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ <= expected_), "%s",                                 \
        ("(" #actual " <= " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_LE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_((actual_ <= expected_), "%s",                                \
        ("(" #actual " <= " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define EXPECT_GT(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ > expected_), "%s",                                  \
        ("(" #actual " > " #expected ")"));                                   \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_GT(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_((actual_ > expected_), "%s",                                 \
        ("(" #actual " > " #expected ")"));                                   \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define EXPECT_GE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ >= expected_), "%s",                                 \
        ("(" #actual " >= " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_GE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_((actual_ >= expected_), "%s",                                \
        ("(" #actual " >= " #expected ")"));                                  \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define EXPECT_TRUE(actual)                                                   \
  do {                                                                        \
    const bool actual_ = (actual) ? true : false;                             \
    TEST_CHECK_((actual_), "%s", ("(" #actual ")"));                          \
    VALUE_MSG(actual_, true);                                                 \
  } while (false);

#define EXPECT_FALSE(actual)                                                  \
  do {                                                                        \
    const bool actual_ = (actual) ? true : false;                             \
    TEST_CHECK_((!actual_), "%s", ("!(" #actual ")"));                        \
    VALUE_MSG(actual_, false);                                                \
  } while (false);

#define EXPECT_THROW(call, exc) TEST_EXCEPTION((void)(call), exc)

#if defined(__GNUC__) || defined(__clang__)
// Supports 0-10 arguments
#define VA_NARGS_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12,  \
    _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26,     \
    _27, _28, _29, _30, N, ...)                                               \
  N
// ## deletes preceding comma if _VA_ARGS__ is empty (GCC, Clang)
#define VA_NARGS(...)                                                         \
  VA_NARGS_IMPL(_, ##__VA_ARGS__, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, \
      19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#else
// Supports 1-10 arguments
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
    _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27,     \
    _28, _29, _30, N, ...)                                                    \
  N
#define VA_NARGS(...)                                                         \
  VA_NARGS_IMPL(__VA_ARGS__, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,  \
      18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#endif

#define VA_NARGS2(...) ((int)(sizeof((int[]){__VA_ARGS__}) / sizeof(int)))

#define TEST_LIST_IMPL_0(x)
#define TEST_LIST_IMPL_1(x) {#x, x},
#define TEST_LIST_IMPL_2(x, ...) {#x, x}, TEST_LIST_IMPL_1(__VA_ARGS__)
#define TEST_LIST_IMPL_3(x, ...) {#x, x}, TEST_LIST_IMPL_2(__VA_ARGS__)
#define TEST_LIST_IMPL_4(x, ...) {#x, x}, TEST_LIST_IMPL_3(__VA_ARGS__)
#define TEST_LIST_IMPL_5(x, ...) {#x, x}, TEST_LIST_IMPL_4(__VA_ARGS__)
#define TEST_LIST_IMPL_6(x, ...) {#x, x}, TEST_LIST_IMPL_5(__VA_ARGS__)
#define TEST_LIST_IMPL_7(x, ...) {#x, x}, TEST_LIST_IMPL_6(__VA_ARGS__)
#define TEST_LIST_IMPL_8(x, ...) {#x, x}, TEST_LIST_IMPL_7(__VA_ARGS__)
#define TEST_LIST_IMPL_9(x, ...) {#x, x}, TEST_LIST_IMPL_8(__VA_ARGS__)
#define TEST_LIST_IMPL_10(x, ...) {#x, x}, TEST_LIST_IMPL_9(__VA_ARGS__)
#define TEST_LIST_IMPL_11(x, ...) {#x, x}, TEST_LIST_IMPL_10(__VA_ARGS__)
#define TEST_LIST_IMPL_12(x, ...) {#x, x}, TEST_LIST_IMPL_11(__VA_ARGS__)
#define TEST_LIST_IMPL_13(x, ...) {#x, x}, TEST_LIST_IMPL_12(__VA_ARGS__)
#define TEST_LIST_IMPL_14(x, ...) {#x, x}, TEST_LIST_IMPL_13(__VA_ARGS__)
#define TEST_LIST_IMPL_15(x, ...) {#x, x}, TEST_LIST_IMPL_14(__VA_ARGS__)
#define TEST_LIST_IMPL_16(x, ...) {#x, x}, TEST_LIST_IMPL_15(__VA_ARGS__)
#define TEST_LIST_IMPL_17(x, ...) {#x, x}, TEST_LIST_IMPL_16(__VA_ARGS__)
#define TEST_LIST_IMPL_18(x, ...) {#x, x}, TEST_LIST_IMPL_17(__VA_ARGS__)
#define TEST_LIST_IMPL_19(x, ...) {#x, x}, TEST_LIST_IMPL_18(__VA_ARGS__)
#define TEST_LIST_IMPL_20(x, ...) {#x, x}, TEST_LIST_IMPL_19(__VA_ARGS__)
#define TEST_LIST_IMPL_21(x, ...) {#x, x}, TEST_LIST_IMPL_20(__VA_ARGS__)
#define TEST_LIST_IMPL_22(x, ...) {#x, x}, TEST_LIST_IMPL_21(__VA_ARGS__)
#define TEST_LIST_IMPL_23(x, ...) {#x, x}, TEST_LIST_IMPL_22(__VA_ARGS__)
#define TEST_LIST_IMPL_24(x, ...) {#x, x}, TEST_LIST_IMPL_23(__VA_ARGS__)
#define TEST_LIST_IMPL_25(x, ...) {#x, x}, TEST_LIST_IMPL_24(__VA_ARGS__)
#define TEST_LIST_IMPL_26(x, ...) {#x, x}, TEST_LIST_IMPL_25(__VA_ARGS__)
#define TEST_LIST_IMPL_27(x, ...) {#x, x}, TEST_LIST_IMPL_26(__VA_ARGS__)
#define TEST_LIST_IMPL_28(x, ...) {#x, x}, TEST_LIST_IMPL_27(__VA_ARGS__)
#define TEST_LIST_IMPL_29(x, ...) {#x, x}, TEST_LIST_IMPL_28(__VA_ARGS__)
#define TEST_LIST_IMPL_30(x, ...) {#x, x}, TEST_LIST_IMPL_29(__VA_ARGS__)

#define TEST_LIST_IMPL_N(N, ...) TEST_LIST_IMPL_##N(__VA_ARGS__)
#define TEST_LIST_IMPL(N, ...) TEST_LIST_IMPL_N(N, __VA_ARGS__)

#define MAKE_TEST_LIST(...)                                                   \
  TEST_LIST = {                                                               \
      TEST_LIST_IMPL(VA_NARGS(__VA_ARGS__), __VA_ARGS__){nullptr, nullptr}};
