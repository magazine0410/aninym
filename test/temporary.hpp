#pragma once

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace aninym::test {

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string{base && *base ? base : "/tmp"} + "/aninym-test-XXXXXX";
    if (::mkdtemp(pattern.data())) path_ = pattern;
  }
  ~TemporaryDirectory() {
    std::error_code error;
    if (!path_.empty()) std::filesystem::remove_all(path_, error);
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  const std::filesystem::path& path() const {
    return path_;
  }

private:
  std::filesystem::path path_;
};

}  // namespace aninym::test
