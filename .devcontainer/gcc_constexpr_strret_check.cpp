// Build-time check that the gcc in the image carries
// gcc-constexpr-strret.patch. Unpatched gcc 13 through 16.2.0 (and trunk as
// of 2026-08-29) fails the offset lines in every dialect: the constexpr
// evaluator counts a nonzero source offset twice in the pointer these builtins
// return. C++26 exposes it through std::string_view::find on a constant
// haystack. The Dockerfile compiles this with -fsyntax-only right after
// installing gcc; a failure means the patch did not take (or, after a gcc bump
// that includes an upstream fix, that the patch can be retired).
constexpr const char a[] = "abcdefghijabxdefghijaaa";
static_assert(__builtin_memchr(a, 'x', sizeof(a) - 1) == a + 12, "+0");
static_assert(__builtin_memchr(a + 1, 'a', sizeof(a) - 2) == a + 10, "+1");
static_assert(__builtin_memchr(a + 2, 'a', sizeof(a) - 3) == a + 10, "+2");
static_assert(__builtin_memchr(a + 1, 'q', sizeof(a) - 2) == nullptr, "miss");
static_assert(__builtin_strchr(a + 1, 'a') == a + 10, "strchr");
static_assert(__builtin_strrchr(a + 1, 'b') == a + 11, "strrchr");
static_assert(__builtin_strstr(a + 1, "ab") == a + 10, "strstr");
static_assert(__builtin_strlen(a + 1) == 22, "strlen, never affected");
