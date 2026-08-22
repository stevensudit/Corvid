// Unit test for corvid::sdl::sdl_status (corvid/sdl/sdl_status.h): a true
// result is ok with an empty message, a failed SDL call captures its error,
// and both `or_throw` forms throw that message as a runtime_error.

#include <stdexcept>
#include <string>

#include "corvid/sdl/sdl_status.h"
#include "catch2_main.h"

using corvid::sdl::sdl_status;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region sdl_status

TEST_CASE("sdl_status captures success and failure", "[sdl]") {
  const sdl_status ok{true};
  CHECK(ok);
  CHECK(ok.ok());
  CHECK(ok.message().empty());
  CHECK(ok.or_throw());

  // A null window fails without any subsystem initialized.
  const sdl_status failed{SDL_SetWindowMinimumSize(nullptr, 1, 1)};
  CHECK_FALSE(failed);
  CHECK_FALSE(failed.message().empty());
  try {
    failed.or_throw();
    FAIL("or_throw did not throw");
  }
  catch (const std::runtime_error& e) {
    CHECK(e.what() == std::string{failed.message()});
  }
}

TEST_CASE("sdl_status::or_throw passes a pointer through or throws", "[sdl]") {
  int value = 1;
  CHECK(sdl_status::or_throw(&value) == &value);

  SDL_SetError("probe");
  int* null = nullptr;
  try {
    (void)sdl_status::or_throw(null);
    FAIL("or_throw did not throw");
  }
  catch (const std::runtime_error& e) {
    CHECK(e.what() == std::string{"probe"});
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
