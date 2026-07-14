#pragma once

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace test {

inline int failures = 0;

inline const std::byte *bytes(const unsigned char *p) {
  return reinterpret_cast<const std::byte *>(p);
}

// empty result means the file could not be read
inline std::vector<std::byte> read_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return {};
  std::vector<std::byte> data(static_cast<std::size_t>(f.tellg()));
  f.seekg(0);
  f.read(reinterpret_cast<char *>(data.data()),
         static_cast<std::streamsize>(data.size()));
  if (!f)
    data.clear();
  return data;
}

inline int report(const char *suite) {
  if (failures != 0) {
    std::printf("%s: %d check(s) failed\n", suite, failures);
    return 1;
  }
  std::printf("%s: all tests passed\n", suite);
  return 0;
}

} // namespace test

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++test::failures;                                                        \
    }                                                                          \
  } while (0)
