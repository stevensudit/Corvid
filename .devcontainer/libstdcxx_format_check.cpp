// Build-time check that the image's libstdc++ <format> carries
// libstdcxx-format-clang.patch. The Dockerfile compiles this with clang++
// -std=c++26 -fsyntax-only right after applying the patch; on the unpatched
// header clang rejects both cases ("undefined function __check_dynamic_spec
// cannot be used in a constant expression" for the dynamic width, and a
// non-constant static_assert for the user formatter's check_dynamic_spec).
// gcc accepts both with or without the patch, so the check is clang-only by
// design.
#include <format>
#include <string>

struct point {
  int x;
};

template<>
struct std::formatter<point, char> {
  std::size_t width_arg{};

  constexpr auto parse(std::format_parse_context& pc) {
    auto it = pc.begin();
    if (it != pc.end() && *it == '{') {
      ++it;
      width_arg = pc.next_arg_id();
      pc.check_dynamic_spec<int, unsigned>(width_arg);
      if (it != pc.end() && *it == '}') ++it;
    }
    return it;
  }

  auto format(point p, std::format_context& fc) const {
    return std::format_to(fc.out(), "({}:{})", p.x, width_arg);
  }
};

int main() {
  std::string dynamic_width = std::format("{:{}}|{:.{}f}", 42, 6, 3.14159, 2);
  std::string user_formatter = std::format("{:{}}", point{7}, 5u);
  return dynamic_width.size() + user_formatter.size();
}
