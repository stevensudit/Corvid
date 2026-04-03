#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <type_traits>

// NOLINTBEGIN

// Minimal test utilities, replacing the external Acutest dependency.
namespace minitest {

struct test {
  const char* name;
  void (*func)();
};

extern test TEST_LIST[];
inline bool current_failed = false;
inline bool just_failed = false;
inline int failed_tests = 0;

inline void mark_failed() {
  current_failed = true;
  just_failed = true;
}

struct assertion_failure final {};

[[noreturn]] inline void abort_failed() { throw assertion_failure{}; }

inline bool was_failed() {
  bool failed{just_failed};
  just_failed = false;
  return failed;
}

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

} // namespace minitest

#define TEST_MSG(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)

#define TEST_CHECK_(cond, fmt, ...)                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      minitest::mark_failed();                                                \
      std::printf("Check failed at %s:%d: " fmt "\n", __FILE__, __LINE__,     \
          ##__VA_ARGS__);                                                     \
    }                                                                         \
  } while (false)

#define TEST_ASSERT_(cond, fmt, ...)                                          \
  do {                                                                        \
    if (!(cond)) {                                                            \
      minitest::mark_failed();                                                \
      std::printf("Assertion failed at %s:%d: " fmt "\n", __FILE__, __LINE__, \
          ##__VA_ARGS__);                                                     \
      minitest::abort_failed();                                               \
    }                                                                         \
  } while (false)

// Like TEST_ASSERT_, but prints actual/expected values before throwing.
#define TEST_ASSERT_VALUE_(cond, actual, expected, fmt, ...)                  \
  do {                                                                        \
    if (!(cond)) {                                                            \
      minitest::mark_failed();                                                \
      std::printf("Assertion failed at %s:%d: " fmt "\n", __FILE__, __LINE__, \
          ##__VA_ARGS__);                                                     \
      TEST_MSG("Actual:   `%s`", minitest::stream_to_text(actual).c_str());   \
      TEST_MSG("Expected: `%s`", minitest::stream_to_text(expected).c_str()); \
      minitest::abort_failed();                                               \
    }                                                                         \
  } while (false)

#define TEST_EXCEPTION(call, exc)                                             \
  do {                                                                        \
    bool caught_ = false;                                                     \
    try {                                                                     \
      (void)(call);                                                           \
    }                                                                         \
    catch (const exc&) {                                                      \
      caught_ = true;                                                         \
    }                                                                         \
    catch (const minitest::assertion_failure&) {                              \
      throw;                                                                  \
    }                                                                         \
    catch (...) {                                                             \
      minitest::mark_failed();                                                \
      std::printf("Unexpected exception at %s:%d\n", __FILE__, __LINE__);     \
      caught_ = true;                                                         \
    }                                                                         \
    if (!caught_) {                                                           \
      minitest::mark_failed();                                                \
      std::printf("Expected exception %s not thrown at %s:%d\n", #exc,        \
          __FILE__, __LINE__);                                                \
    }                                                                         \
  } while (false)

#define VALUE_MSG(actual, expected)                                           \
  if (minitest::was_failed()) {                                               \
    TEST_MSG("Actual:   `%s`", minitest::stream_to_text(actual).c_str());     \
    TEST_MSG("Expected: `%s`", minitest::stream_to_text(expected).c_str());   \
    TEST_MSG("File: %s Line: %d Function: %s", __FILE__, __LINE__,            \
        __FUNCTION__);                                                        \
  }

// Like VALUE_MSG, but for NE checks: labels the second value "Unexpected".
#define VALUE_NE_MSG(actual, expected)                                        \
  if (minitest::was_failed()) {                                               \
    TEST_MSG("Actual:     `%s`", minitest::stream_to_text(actual).c_str());   \
    TEST_MSG("Unexpected: `%s`", minitest::stream_to_text(expected).c_str()); \
    TEST_MSG("File: %s Line: %d Function: %s", __FILE__, __LINE__,            \
        __FUNCTION__);                                                        \
  }

// Like TEST_ASSERT_VALUE_, but for NE checks: labels the second value
// "Unexpected".
#define TEST_ASSERT_VALUE_NE_(cond, actual, expected, fmt, ...)               \
  do {                                                                        \
    if (!(cond)) {                                                            \
      minitest::mark_failed();                                                \
      std::printf("Assertion failed at %s:%d: " fmt "\n", __FILE__, __LINE__, \
          ##__VA_ARGS__);                                                     \
      TEST_MSG("Actual:     `%s`", minitest::stream_to_text(actual).c_str()); \
      TEST_MSG("Unexpected: `%s`",                                            \
          minitest::stream_to_text(expected).c_str());                        \
      minitest::abort_failed();                                               \
    }                                                                         \
  } while (false)

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
    TEST_ASSERT_VALUE_((actual_ == expected_), actual_, expected_, "%s",      \
        ("(" #actual " == " #expected ")"));                                  \
  } while (false);

#define EXPECT_NE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_((actual_ != expected_), "%s",                                 \
        ("(" #actual " != " #expected ")"));                                  \
    VALUE_NE_MSG(actual_, expected_);                                         \
  } while (false);

#define ASSERT_NE(actual, expected)                                           \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_VALUE_NE_((actual_ != expected_), actual_, expected_, "%s",   \
        ("(" #actual " != " #expected ")"));                                  \
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
    TEST_ASSERT_VALUE_((actual_ < expected_), actual_, expected_, "%s",       \
        ("(" #actual " < " #expected ")"));                                   \
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
    TEST_ASSERT_VALUE_((actual_ <= expected_), actual_, expected_, "%s",      \
        ("(" #actual " <= " #expected ")"));                                  \
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
    TEST_ASSERT_VALUE_((actual_ > expected_), actual_, expected_, "%s",       \
        ("(" #actual " > " #expected ")"));                                   \
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
    TEST_ASSERT_VALUE_((actual_ >= expected_), actual_, expected_, "%s",      \
        ("(" #actual " >= " #expected ")"));                                  \
  } while (false);

#define EXPECT_TRUE(actual)                                                   \
  do {                                                                        \
    const bool actual_ = (actual) ? true : false;                             \
    TEST_CHECK_((actual_), "%s", ("(" #actual ")"));                          \
    VALUE_MSG(actual_, true);                                                 \
  } while (false);

#define ASSERT_TRUE(actual)                                                   \
  do {                                                                        \
    const bool actual_ = (actual) ? true : false;                             \
    TEST_ASSERT_VALUE_((actual_), actual_, true, "%s", ("(" #actual ")"));    \
  } while (false);

#define EXPECT_FALSE(actual)                                                  \
  do {                                                                        \
    const bool actual_ = (actual) ? true : false;                             \
    TEST_CHECK_((!actual_), "%s", ("!(" #actual ")"));                        \
    VALUE_MSG(actual_, false);                                                \
  } while (false);

#define ASSERT_FALSE(actual)                                                  \
  do {                                                                        \
    const bool actual_ = (actual) ? true : false;                             \
    TEST_ASSERT_VALUE_((!actual_), actual_, false, "%s", ("!(" #actual ")")); \
  } while (false);

#define EXPECT_NEAR(actual, expected, abs_error)                              \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_CHECK_(std::abs(actual_ - expected_) <= abs_error, "%s",             \
        ("std::abs(" #actual " - " #expected ") <= " #abs_error));            \
    VALUE_MSG(actual_, expected_);                                            \
  } while (false);

#define ASSERT_NEAR(actual, expected, abs_error)                              \
  do {                                                                        \
    const auto& actual_ = (actual);                                           \
    const auto& expected_ = (expected);                                       \
    TEST_ASSERT_VALUE_(std::abs(actual_ - expected_) <= abs_error, actual_,   \
        expected_, "%s",                                                      \
        ("std::abs(" #actual " - " #expected ") <= " #abs_error));            \
  } while (false);

#define EXPECT_THROW(call, exc) TEST_EXCEPTION((void)(call), exc)

#define ASSERT_THROW(call, exc)                                               \
  do {                                                                        \
    bool caught_ = false;                                                     \
    try {                                                                     \
      (void)(call);                                                           \
    }                                                                         \
    catch (const exc&) {                                                      \
      caught_ = true;                                                         \
    }                                                                         \
    catch (const minitest::assertion_failure&) {                              \
      throw;                                                                  \
    }                                                                         \
    catch (...) {                                                             \
      minitest::mark_failed();                                                \
      std::printf("Unexpected exception at %s:%d\n", __FILE__, __LINE__);     \
      minitest::abort_failed();                                               \
    }                                                                         \
    if (!caught_) {                                                           \
      minitest::mark_failed();                                                \
      std::printf("Expected exception %s not thrown at %s:%d\n", #exc,        \
          __FILE__, __LINE__);                                                \
      minitest::abort_failed();                                               \
    }                                                                         \
  } while (false);

#if defined(__GNUC__) || defined(__clang__)
// Supports 0-200 arguments
#define VA_NARGS_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12,  \
    _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26,     \
    _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40,     \
    _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
    _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68,     \
    _69, _70, _71, _72, _73, _74, _75, _76, _77, _78, _79, _80, _81, _82,     \
    _83, _84, _85, _86, _87, _88, _89, _90, _91, _92, _93, _94, _95, _96,     \
    _97, _98, _99, _100, _101, _102, _103, _104, _105, _106, _107, _108,      \
    _109, _110, _111, _112, _113, _114, _115, _116, _117, _118, _119, _120,   \
    _121, _122, _123, _124, _125, _126, _127, _128, _129, _130, _131, _132,   \
    _133, _134, _135, _136, _137, _138, _139, _140, _141, _142, _143, _144,   \
    _145, _146, _147, _148, _149, _150, _151, _152, _153, _154, _155, _156,   \
    _157, _158, _159, _160, _161, _162, _163, _164, _165, _166, _167, _168,   \
    _169, _170, _171, _172, _173, _174, _175, _176, _177, _178, _179, _180,   \
    _181, _182, _183, _184, _185, _186, _187, _188, _189, _190, _191, _192,   \
    _193, _194, _195, _196, _197, _198, _199, _200, N, ...)                   \
  N
// ## deletes preceding comma if __VA_ARGS__ is empty (GCC, Clang)
#define VA_NARGS(...)                                                         \
  VA_NARGS_IMPL(_, ##__VA_ARGS__, 200, 199, 198, 197, 196, 195, 194, 193,     \
      192, 191, 190, 189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179,   \
      178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 165,   \
      164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151,   \
      150, 149, 148, 147, 146, 145, 144, 143, 142, 141, 140, 139, 138, 137,   \
      136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124, 123,   \
      122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109,   \
      108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94,    \
      93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, \
      75, 74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, \
      57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, \
      39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, \
      21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, \
      1, 0)
#else
// Supports 1-200 arguments
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
    _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27,     \
    _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
    _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55,     \
    _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69,     \
    _70, _71, _72, _73, _74, _75, _76, _77, _78, _79, _80, _81, _82, _83,     \
    _84, _85, _86, _87, _88, _89, _90, _91, _92, _93, _94, _95, _96, _97,     \
    _98, _99, _100, _101, _102, _103, _104, _105, _106, _107, _108, _109,     \
    _110, _111, _112, _113, _114, _115, _116, _117, _118, _119, _120, _121,   \
    _122, _123, _124, _125, _126, _127, _128, _129, _130, _131, _132, _133,   \
    _134, _135, _136, _137, _138, _139, _140, _141, _142, _143, _144, _145,   \
    _146, _147, _148, _149, _150, _151, _152, _153, _154, _155, _156, _157,   \
    _158, _159, _160, _161, _162, _163, _164, _165, _166, _167, _168, _169,   \
    _170, _171, _172, _173, _174, _175, _176, _177, _178, _179, _180, _181,   \
    _182, _183, _184, _185, _186, _187, _188, _189, _190, _191, _192, _193,   \
    _194, _195, _196, _197, _198, _199, _200, N, ...)                         \
  N
#define VA_NARGS(...)                                                         \
  VA_NARGS_IMPL(__VA_ARGS__, 200, 199, 198, 197, 196, 195, 194, 193, 192,     \
      191, 190, 189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178,   \
      177, 176, 175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 165, 164,   \
      163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150,   \
      149, 148, 147, 146, 145, 144, 143, 142, 141, 140, 139, 138, 137, 136,   \
      135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124, 123, 122,   \
      121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108,   \
      107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92, \
      91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, \
      73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, \
      55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, \
      37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, \
      19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
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
#define TEST_LIST_IMPL_31(x, ...) {#x, x}, TEST_LIST_IMPL_30(__VA_ARGS__)
#define TEST_LIST_IMPL_32(x, ...) {#x, x}, TEST_LIST_IMPL_31(__VA_ARGS__)
#define TEST_LIST_IMPL_33(x, ...) {#x, x}, TEST_LIST_IMPL_32(__VA_ARGS__)
#define TEST_LIST_IMPL_34(x, ...) {#x, x}, TEST_LIST_IMPL_33(__VA_ARGS__)
#define TEST_LIST_IMPL_35(x, ...) {#x, x}, TEST_LIST_IMPL_34(__VA_ARGS__)
#define TEST_LIST_IMPL_36(x, ...) {#x, x}, TEST_LIST_IMPL_35(__VA_ARGS__)
#define TEST_LIST_IMPL_37(x, ...) {#x, x}, TEST_LIST_IMPL_36(__VA_ARGS__)
#define TEST_LIST_IMPL_38(x, ...) {#x, x}, TEST_LIST_IMPL_37(__VA_ARGS__)
#define TEST_LIST_IMPL_39(x, ...) {#x, x}, TEST_LIST_IMPL_38(__VA_ARGS__)
#define TEST_LIST_IMPL_40(x, ...) {#x, x}, TEST_LIST_IMPL_39(__VA_ARGS__)
#define TEST_LIST_IMPL_41(x, ...) {#x, x}, TEST_LIST_IMPL_40(__VA_ARGS__)
#define TEST_LIST_IMPL_42(x, ...) {#x, x}, TEST_LIST_IMPL_41(__VA_ARGS__)
#define TEST_LIST_IMPL_43(x, ...) {#x, x}, TEST_LIST_IMPL_42(__VA_ARGS__)
#define TEST_LIST_IMPL_44(x, ...) {#x, x}, TEST_LIST_IMPL_43(__VA_ARGS__)
#define TEST_LIST_IMPL_45(x, ...) {#x, x}, TEST_LIST_IMPL_44(__VA_ARGS__)
#define TEST_LIST_IMPL_46(x, ...) {#x, x}, TEST_LIST_IMPL_45(__VA_ARGS__)
#define TEST_LIST_IMPL_47(x, ...) {#x, x}, TEST_LIST_IMPL_46(__VA_ARGS__)
#define TEST_LIST_IMPL_48(x, ...) {#x, x}, TEST_LIST_IMPL_47(__VA_ARGS__)
#define TEST_LIST_IMPL_49(x, ...) {#x, x}, TEST_LIST_IMPL_48(__VA_ARGS__)
#define TEST_LIST_IMPL_50(x, ...) {#x, x}, TEST_LIST_IMPL_49(__VA_ARGS__)
#define TEST_LIST_IMPL_51(x, ...) {#x, x}, TEST_LIST_IMPL_50(__VA_ARGS__)
#define TEST_LIST_IMPL_52(x, ...) {#x, x}, TEST_LIST_IMPL_51(__VA_ARGS__)
#define TEST_LIST_IMPL_53(x, ...) {#x, x}, TEST_LIST_IMPL_52(__VA_ARGS__)
#define TEST_LIST_IMPL_54(x, ...) {#x, x}, TEST_LIST_IMPL_53(__VA_ARGS__)
#define TEST_LIST_IMPL_55(x, ...) {#x, x}, TEST_LIST_IMPL_54(__VA_ARGS__)
#define TEST_LIST_IMPL_56(x, ...) {#x, x}, TEST_LIST_IMPL_55(__VA_ARGS__)
#define TEST_LIST_IMPL_57(x, ...) {#x, x}, TEST_LIST_IMPL_56(__VA_ARGS__)
#define TEST_LIST_IMPL_58(x, ...) {#x, x}, TEST_LIST_IMPL_57(__VA_ARGS__)
#define TEST_LIST_IMPL_59(x, ...) {#x, x}, TEST_LIST_IMPL_58(__VA_ARGS__)
#define TEST_LIST_IMPL_60(x, ...) {#x, x}, TEST_LIST_IMPL_59(__VA_ARGS__)
#define TEST_LIST_IMPL_61(x, ...) {#x, x}, TEST_LIST_IMPL_60(__VA_ARGS__)
#define TEST_LIST_IMPL_62(x, ...) {#x, x}, TEST_LIST_IMPL_61(__VA_ARGS__)
#define TEST_LIST_IMPL_63(x, ...) {#x, x}, TEST_LIST_IMPL_62(__VA_ARGS__)
#define TEST_LIST_IMPL_64(x, ...) {#x, x}, TEST_LIST_IMPL_63(__VA_ARGS__)
#define TEST_LIST_IMPL_65(x, ...) {#x, x}, TEST_LIST_IMPL_64(__VA_ARGS__)
#define TEST_LIST_IMPL_66(x, ...) {#x, x}, TEST_LIST_IMPL_65(__VA_ARGS__)
#define TEST_LIST_IMPL_67(x, ...) {#x, x}, TEST_LIST_IMPL_66(__VA_ARGS__)
#define TEST_LIST_IMPL_68(x, ...) {#x, x}, TEST_LIST_IMPL_67(__VA_ARGS__)
#define TEST_LIST_IMPL_69(x, ...) {#x, x}, TEST_LIST_IMPL_68(__VA_ARGS__)
#define TEST_LIST_IMPL_70(x, ...) {#x, x}, TEST_LIST_IMPL_69(__VA_ARGS__)
#define TEST_LIST_IMPL_71(x, ...) {#x, x}, TEST_LIST_IMPL_70(__VA_ARGS__)
#define TEST_LIST_IMPL_72(x, ...) {#x, x}, TEST_LIST_IMPL_71(__VA_ARGS__)
#define TEST_LIST_IMPL_73(x, ...) {#x, x}, TEST_LIST_IMPL_72(__VA_ARGS__)
#define TEST_LIST_IMPL_74(x, ...) {#x, x}, TEST_LIST_IMPL_73(__VA_ARGS__)
#define TEST_LIST_IMPL_75(x, ...) {#x, x}, TEST_LIST_IMPL_74(__VA_ARGS__)
#define TEST_LIST_IMPL_76(x, ...) {#x, x}, TEST_LIST_IMPL_75(__VA_ARGS__)
#define TEST_LIST_IMPL_77(x, ...) {#x, x}, TEST_LIST_IMPL_76(__VA_ARGS__)
#define TEST_LIST_IMPL_78(x, ...) {#x, x}, TEST_LIST_IMPL_77(__VA_ARGS__)
#define TEST_LIST_IMPL_79(x, ...) {#x, x}, TEST_LIST_IMPL_78(__VA_ARGS__)
#define TEST_LIST_IMPL_80(x, ...) {#x, x}, TEST_LIST_IMPL_79(__VA_ARGS__)
#define TEST_LIST_IMPL_81(x, ...) {#x, x}, TEST_LIST_IMPL_80(__VA_ARGS__)
#define TEST_LIST_IMPL_82(x, ...) {#x, x}, TEST_LIST_IMPL_81(__VA_ARGS__)
#define TEST_LIST_IMPL_83(x, ...) {#x, x}, TEST_LIST_IMPL_82(__VA_ARGS__)
#define TEST_LIST_IMPL_84(x, ...) {#x, x}, TEST_LIST_IMPL_83(__VA_ARGS__)
#define TEST_LIST_IMPL_85(x, ...) {#x, x}, TEST_LIST_IMPL_84(__VA_ARGS__)
#define TEST_LIST_IMPL_86(x, ...) {#x, x}, TEST_LIST_IMPL_85(__VA_ARGS__)
#define TEST_LIST_IMPL_87(x, ...) {#x, x}, TEST_LIST_IMPL_86(__VA_ARGS__)
#define TEST_LIST_IMPL_88(x, ...) {#x, x}, TEST_LIST_IMPL_87(__VA_ARGS__)
#define TEST_LIST_IMPL_89(x, ...) {#x, x}, TEST_LIST_IMPL_88(__VA_ARGS__)
#define TEST_LIST_IMPL_90(x, ...) {#x, x}, TEST_LIST_IMPL_89(__VA_ARGS__)
#define TEST_LIST_IMPL_91(x, ...) {#x, x}, TEST_LIST_IMPL_90(__VA_ARGS__)
#define TEST_LIST_IMPL_92(x, ...) {#x, x}, TEST_LIST_IMPL_91(__VA_ARGS__)
#define TEST_LIST_IMPL_93(x, ...) {#x, x}, TEST_LIST_IMPL_92(__VA_ARGS__)
#define TEST_LIST_IMPL_94(x, ...) {#x, x}, TEST_LIST_IMPL_93(__VA_ARGS__)
#define TEST_LIST_IMPL_95(x, ...) {#x, x}, TEST_LIST_IMPL_94(__VA_ARGS__)
#define TEST_LIST_IMPL_96(x, ...) {#x, x}, TEST_LIST_IMPL_95(__VA_ARGS__)
#define TEST_LIST_IMPL_97(x, ...) {#x, x}, TEST_LIST_IMPL_96(__VA_ARGS__)
#define TEST_LIST_IMPL_98(x, ...) {#x, x}, TEST_LIST_IMPL_97(__VA_ARGS__)
#define TEST_LIST_IMPL_99(x, ...) {#x, x}, TEST_LIST_IMPL_98(__VA_ARGS__)
#define TEST_LIST_IMPL_100(x, ...) {#x, x}, TEST_LIST_IMPL_99(__VA_ARGS__)
#define TEST_LIST_IMPL_101(x, ...) {#x, x}, TEST_LIST_IMPL_100(__VA_ARGS__)
#define TEST_LIST_IMPL_102(x, ...) {#x, x}, TEST_LIST_IMPL_101(__VA_ARGS__)
#define TEST_LIST_IMPL_103(x, ...) {#x, x}, TEST_LIST_IMPL_102(__VA_ARGS__)
#define TEST_LIST_IMPL_104(x, ...) {#x, x}, TEST_LIST_IMPL_103(__VA_ARGS__)
#define TEST_LIST_IMPL_105(x, ...) {#x, x}, TEST_LIST_IMPL_104(__VA_ARGS__)
#define TEST_LIST_IMPL_106(x, ...) {#x, x}, TEST_LIST_IMPL_105(__VA_ARGS__)
#define TEST_LIST_IMPL_107(x, ...) {#x, x}, TEST_LIST_IMPL_106(__VA_ARGS__)
#define TEST_LIST_IMPL_108(x, ...) {#x, x}, TEST_LIST_IMPL_107(__VA_ARGS__)
#define TEST_LIST_IMPL_109(x, ...) {#x, x}, TEST_LIST_IMPL_108(__VA_ARGS__)
#define TEST_LIST_IMPL_110(x, ...) {#x, x}, TEST_LIST_IMPL_109(__VA_ARGS__)
#define TEST_LIST_IMPL_111(x, ...) {#x, x}, TEST_LIST_IMPL_110(__VA_ARGS__)
#define TEST_LIST_IMPL_112(x, ...) {#x, x}, TEST_LIST_IMPL_111(__VA_ARGS__)
#define TEST_LIST_IMPL_113(x, ...) {#x, x}, TEST_LIST_IMPL_112(__VA_ARGS__)
#define TEST_LIST_IMPL_114(x, ...) {#x, x}, TEST_LIST_IMPL_113(__VA_ARGS__)
#define TEST_LIST_IMPL_115(x, ...) {#x, x}, TEST_LIST_IMPL_114(__VA_ARGS__)
#define TEST_LIST_IMPL_116(x, ...) {#x, x}, TEST_LIST_IMPL_115(__VA_ARGS__)
#define TEST_LIST_IMPL_117(x, ...) {#x, x}, TEST_LIST_IMPL_116(__VA_ARGS__)
#define TEST_LIST_IMPL_118(x, ...) {#x, x}, TEST_LIST_IMPL_117(__VA_ARGS__)
#define TEST_LIST_IMPL_119(x, ...) {#x, x}, TEST_LIST_IMPL_118(__VA_ARGS__)
#define TEST_LIST_IMPL_120(x, ...) {#x, x}, TEST_LIST_IMPL_119(__VA_ARGS__)
#define TEST_LIST_IMPL_121(x, ...) {#x, x}, TEST_LIST_IMPL_120(__VA_ARGS__)
#define TEST_LIST_IMPL_122(x, ...) {#x, x}, TEST_LIST_IMPL_121(__VA_ARGS__)
#define TEST_LIST_IMPL_123(x, ...) {#x, x}, TEST_LIST_IMPL_122(__VA_ARGS__)
#define TEST_LIST_IMPL_124(x, ...) {#x, x}, TEST_LIST_IMPL_123(__VA_ARGS__)
#define TEST_LIST_IMPL_125(x, ...) {#x, x}, TEST_LIST_IMPL_124(__VA_ARGS__)
#define TEST_LIST_IMPL_126(x, ...) {#x, x}, TEST_LIST_IMPL_125(__VA_ARGS__)
#define TEST_LIST_IMPL_127(x, ...) {#x, x}, TEST_LIST_IMPL_126(__VA_ARGS__)
#define TEST_LIST_IMPL_128(x, ...) {#x, x}, TEST_LIST_IMPL_127(__VA_ARGS__)
#define TEST_LIST_IMPL_129(x, ...) {#x, x}, TEST_LIST_IMPL_128(__VA_ARGS__)
#define TEST_LIST_IMPL_130(x, ...) {#x, x}, TEST_LIST_IMPL_129(__VA_ARGS__)
#define TEST_LIST_IMPL_131(x, ...) {#x, x}, TEST_LIST_IMPL_130(__VA_ARGS__)
#define TEST_LIST_IMPL_132(x, ...) {#x, x}, TEST_LIST_IMPL_131(__VA_ARGS__)
#define TEST_LIST_IMPL_133(x, ...) {#x, x}, TEST_LIST_IMPL_132(__VA_ARGS__)
#define TEST_LIST_IMPL_134(x, ...) {#x, x}, TEST_LIST_IMPL_133(__VA_ARGS__)
#define TEST_LIST_IMPL_135(x, ...) {#x, x}, TEST_LIST_IMPL_134(__VA_ARGS__)
#define TEST_LIST_IMPL_136(x, ...) {#x, x}, TEST_LIST_IMPL_135(__VA_ARGS__)
#define TEST_LIST_IMPL_137(x, ...) {#x, x}, TEST_LIST_IMPL_136(__VA_ARGS__)
#define TEST_LIST_IMPL_138(x, ...) {#x, x}, TEST_LIST_IMPL_137(__VA_ARGS__)
#define TEST_LIST_IMPL_139(x, ...) {#x, x}, TEST_LIST_IMPL_138(__VA_ARGS__)
#define TEST_LIST_IMPL_140(x, ...) {#x, x}, TEST_LIST_IMPL_139(__VA_ARGS__)
#define TEST_LIST_IMPL_141(x, ...) {#x, x}, TEST_LIST_IMPL_140(__VA_ARGS__)
#define TEST_LIST_IMPL_142(x, ...) {#x, x}, TEST_LIST_IMPL_141(__VA_ARGS__)
#define TEST_LIST_IMPL_143(x, ...) {#x, x}, TEST_LIST_IMPL_142(__VA_ARGS__)
#define TEST_LIST_IMPL_144(x, ...) {#x, x}, TEST_LIST_IMPL_143(__VA_ARGS__)
#define TEST_LIST_IMPL_145(x, ...) {#x, x}, TEST_LIST_IMPL_144(__VA_ARGS__)
#define TEST_LIST_IMPL_146(x, ...) {#x, x}, TEST_LIST_IMPL_145(__VA_ARGS__)
#define TEST_LIST_IMPL_147(x, ...) {#x, x}, TEST_LIST_IMPL_146(__VA_ARGS__)
#define TEST_LIST_IMPL_148(x, ...) {#x, x}, TEST_LIST_IMPL_147(__VA_ARGS__)
#define TEST_LIST_IMPL_149(x, ...) {#x, x}, TEST_LIST_IMPL_148(__VA_ARGS__)
#define TEST_LIST_IMPL_150(x, ...) {#x, x}, TEST_LIST_IMPL_149(__VA_ARGS__)
#define TEST_LIST_IMPL_151(x, ...) {#x, x}, TEST_LIST_IMPL_150(__VA_ARGS__)
#define TEST_LIST_IMPL_152(x, ...) {#x, x}, TEST_LIST_IMPL_151(__VA_ARGS__)
#define TEST_LIST_IMPL_153(x, ...) {#x, x}, TEST_LIST_IMPL_152(__VA_ARGS__)
#define TEST_LIST_IMPL_154(x, ...) {#x, x}, TEST_LIST_IMPL_153(__VA_ARGS__)
#define TEST_LIST_IMPL_155(x, ...) {#x, x}, TEST_LIST_IMPL_154(__VA_ARGS__)
#define TEST_LIST_IMPL_156(x, ...) {#x, x}, TEST_LIST_IMPL_155(__VA_ARGS__)
#define TEST_LIST_IMPL_157(x, ...) {#x, x}, TEST_LIST_IMPL_156(__VA_ARGS__)
#define TEST_LIST_IMPL_158(x, ...) {#x, x}, TEST_LIST_IMPL_157(__VA_ARGS__)
#define TEST_LIST_IMPL_159(x, ...) {#x, x}, TEST_LIST_IMPL_158(__VA_ARGS__)
#define TEST_LIST_IMPL_160(x, ...) {#x, x}, TEST_LIST_IMPL_159(__VA_ARGS__)
#define TEST_LIST_IMPL_161(x, ...) {#x, x}, TEST_LIST_IMPL_160(__VA_ARGS__)
#define TEST_LIST_IMPL_162(x, ...) {#x, x}, TEST_LIST_IMPL_161(__VA_ARGS__)
#define TEST_LIST_IMPL_163(x, ...) {#x, x}, TEST_LIST_IMPL_162(__VA_ARGS__)
#define TEST_LIST_IMPL_164(x, ...) {#x, x}, TEST_LIST_IMPL_163(__VA_ARGS__)
#define TEST_LIST_IMPL_165(x, ...) {#x, x}, TEST_LIST_IMPL_164(__VA_ARGS__)
#define TEST_LIST_IMPL_166(x, ...) {#x, x}, TEST_LIST_IMPL_165(__VA_ARGS__)
#define TEST_LIST_IMPL_167(x, ...) {#x, x}, TEST_LIST_IMPL_166(__VA_ARGS__)
#define TEST_LIST_IMPL_168(x, ...) {#x, x}, TEST_LIST_IMPL_167(__VA_ARGS__)
#define TEST_LIST_IMPL_169(x, ...) {#x, x}, TEST_LIST_IMPL_168(__VA_ARGS__)
#define TEST_LIST_IMPL_170(x, ...) {#x, x}, TEST_LIST_IMPL_169(__VA_ARGS__)
#define TEST_LIST_IMPL_171(x, ...) {#x, x}, TEST_LIST_IMPL_170(__VA_ARGS__)
#define TEST_LIST_IMPL_172(x, ...) {#x, x}, TEST_LIST_IMPL_171(__VA_ARGS__)
#define TEST_LIST_IMPL_173(x, ...) {#x, x}, TEST_LIST_IMPL_172(__VA_ARGS__)
#define TEST_LIST_IMPL_174(x, ...) {#x, x}, TEST_LIST_IMPL_173(__VA_ARGS__)
#define TEST_LIST_IMPL_175(x, ...) {#x, x}, TEST_LIST_IMPL_174(__VA_ARGS__)
#define TEST_LIST_IMPL_176(x, ...) {#x, x}, TEST_LIST_IMPL_175(__VA_ARGS__)
#define TEST_LIST_IMPL_177(x, ...) {#x, x}, TEST_LIST_IMPL_176(__VA_ARGS__)
#define TEST_LIST_IMPL_178(x, ...) {#x, x}, TEST_LIST_IMPL_177(__VA_ARGS__)
#define TEST_LIST_IMPL_179(x, ...) {#x, x}, TEST_LIST_IMPL_178(__VA_ARGS__)
#define TEST_LIST_IMPL_180(x, ...) {#x, x}, TEST_LIST_IMPL_179(__VA_ARGS__)
#define TEST_LIST_IMPL_181(x, ...) {#x, x}, TEST_LIST_IMPL_180(__VA_ARGS__)
#define TEST_LIST_IMPL_182(x, ...) {#x, x}, TEST_LIST_IMPL_181(__VA_ARGS__)
#define TEST_LIST_IMPL_183(x, ...) {#x, x}, TEST_LIST_IMPL_182(__VA_ARGS__)
#define TEST_LIST_IMPL_184(x, ...) {#x, x}, TEST_LIST_IMPL_183(__VA_ARGS__)
#define TEST_LIST_IMPL_185(x, ...) {#x, x}, TEST_LIST_IMPL_184(__VA_ARGS__)
#define TEST_LIST_IMPL_186(x, ...) {#x, x}, TEST_LIST_IMPL_185(__VA_ARGS__)
#define TEST_LIST_IMPL_187(x, ...) {#x, x}, TEST_LIST_IMPL_186(__VA_ARGS__)
#define TEST_LIST_IMPL_188(x, ...) {#x, x}, TEST_LIST_IMPL_187(__VA_ARGS__)
#define TEST_LIST_IMPL_189(x, ...) {#x, x}, TEST_LIST_IMPL_188(__VA_ARGS__)
#define TEST_LIST_IMPL_190(x, ...) {#x, x}, TEST_LIST_IMPL_189(__VA_ARGS__)
#define TEST_LIST_IMPL_191(x, ...) {#x, x}, TEST_LIST_IMPL_190(__VA_ARGS__)
#define TEST_LIST_IMPL_192(x, ...) {#x, x}, TEST_LIST_IMPL_191(__VA_ARGS__)
#define TEST_LIST_IMPL_193(x, ...) {#x, x}, TEST_LIST_IMPL_192(__VA_ARGS__)
#define TEST_LIST_IMPL_194(x, ...) {#x, x}, TEST_LIST_IMPL_193(__VA_ARGS__)
#define TEST_LIST_IMPL_195(x, ...) {#x, x}, TEST_LIST_IMPL_194(__VA_ARGS__)
#define TEST_LIST_IMPL_196(x, ...) {#x, x}, TEST_LIST_IMPL_195(__VA_ARGS__)
#define TEST_LIST_IMPL_197(x, ...) {#x, x}, TEST_LIST_IMPL_196(__VA_ARGS__)
#define TEST_LIST_IMPL_198(x, ...) {#x, x}, TEST_LIST_IMPL_197(__VA_ARGS__)
#define TEST_LIST_IMPL_199(x, ...) {#x, x}, TEST_LIST_IMPL_198(__VA_ARGS__)
#define TEST_LIST_IMPL_200(x, ...) {#x, x}, TEST_LIST_IMPL_199(__VA_ARGS__)

#define TEST_LIST_IMPL_N(N, ...) TEST_LIST_IMPL_##N(__VA_ARGS__)
#define TEST_LIST_IMPL(N, ...) TEST_LIST_IMPL_N(N, __VA_ARGS__)

#define MAKE_TEST_LIST(...)                                                   \
  namespace minitest {                                                        \
  test TEST_LIST[] = {                                                        \
      TEST_LIST_IMPL(VA_NARGS(__VA_ARGS__), __VA_ARGS__){nullptr, nullptr}};  \
  }

int main() {
  using namespace minitest;
  const auto total_timer_start_ = std::chrono::steady_clock::now();
  for (const test* t = TEST_LIST; t->name; ++t) {
    current_failed = false;
    just_failed = false;
    // std::printf("Running %s\n", t->name);
#if defined(MINITEST_SHOW_TIMERS) && MINITEST_SHOW_TIMERS == 1
    auto timer_start_ = std::chrono::steady_clock::now();
#endif
    try {
      t->func();
    }
    catch (const assertion_failure&) {
      mark_failed();
      std::printf("Assertion failure\n");
    }
    catch (const std::exception& e) {
      mark_failed();
      std::printf("Unexpected exception: %s\n", e.what());
    }
    catch (...) {
      mark_failed();
      std::printf("Unknown exception occurred\n");
    }
    if (current_failed) {
      std::printf("[FAIL] %s\n", t->name);
      ++failed_tests;
    } else {
      // std::printf("[PASS] %s\n", t->name);
    }
#if defined(MINITEST_SHOW_TIMERS) && MINITEST_SHOW_TIMERS == 1
    {
      auto timer_elapsed_ = std::chrono::steady_clock::now() - timer_start_;
      double ms_ =
          std::chrono::duration<double, std::milli>(timer_elapsed_).count();
      std::printf("[TIME] %s: %.3f ms\n", t->name, ms_);
    }
#endif
  }
  int exit_code = 0;
  if (failed_tests) {
    std::printf("%d test(s) failed\n", failed_tests);
    exit_code = 1;
  } else {
    std::printf("All tests passed\n");
  }
  {
    auto total_timer_elapsed_ =
        std::chrono::steady_clock::now() - total_timer_start_;
    double total_ms_ =
        std::chrono::duration<double, std::milli>(total_timer_elapsed_)
            .count();
    std::printf("[TIME] Total: %.3f ms\n", total_ms_);
  }
  return exit_code;
}

// NOLINTEND
