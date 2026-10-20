#include <iostream>
#include <string_view>

#include "check.hpp"

// Runs every test, or those whose names contain the first argument.
int main(int argc, char** argv) {
  const std::string_view filter = argc > 1 ? argv[1] : "";
  int run = 0;
  for (const auto& test : aninym::test::cases()) {
    if (std::string_view{test.name}.find(filter) == std::string_view::npos) continue;
    const int before = aninym::test::failures();
    test.run();
    ++run;
    std::cout << (aninym::test::failures() == before ? "PASS " : "FAIL ") << test.name << '\n';
  }
  std::cout << run << " tests, " << aninym::test::failures() << " failed checks\n";
  return aninym::test::failures() ? 1 : 0;
}
