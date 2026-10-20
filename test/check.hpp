#pragma once

// Just enough of a test framework to keep aninym free of dependencies.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace aninym::test {

struct Case {
  const char* name = nullptr;
  std::function<void()> run;
};

inline std::vector<Case>& cases() {
  static std::vector<Case> all;
  return all;
}

inline int& failures() {
  static int count = 0;
  return count;
}

struct Register {
  Register(const char* name, std::function<void()> run) {
    cases().push_back({name, std::move(run)});
  }
};

template <typename T>
void print(std::ostream& out, const T& value) {
  out << value;
}

template <typename T>
void print(std::ostream& out, const std::vector<T>& values) {
  out << '{';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out << ", ";
    print(out, values[i]);
  }
  out << '}';
}

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* expression, const char* file,
                const int line) {
  if (actual == expected) return;
  std::ostringstream message;
  message << file << ':' << line << ": " << expression << "\n    got:      ";
  print(message, actual);
  message << "\n    expected: ";
  print(message, expected);
  message << '\n';
  std::cerr << message.str();
  ++failures();
}

}  // namespace aninym::test

#define ANINYM_JOIN2(a, b) a##b
#define ANINYM_JOIN(a, b) ANINYM_JOIN2(a, b)

#define TEST(name)                                                                \
  static void name();                                                             \
  static const aninym::test::Register ANINYM_JOIN(register_, name){#name, &name}; \
  static void name()

#define CHECK(condition)                                                             \
  do {                                                                               \
    if (!(condition)) {                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << " failed\n"; \
      ++aninym::test::failures();                                                    \
    }                                                                                \
  } while (false)

#define CHECK_EQ(actual, expected) \
  aninym::test::checkEqual((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
