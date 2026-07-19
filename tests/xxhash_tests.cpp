#include "check.hpp"

#include <crunch/xxhash.hpp>

#include <array>
#include <cstdint>

namespace {

std::string data_dir;

// expected hashes from xxhsum 0.8.3 over the byte sequence 0, 1, 2, ...
void test_vectors() {
  std::array<std::byte, 100> buf{};
  for (std::size_t i = 0; i < buf.size(); ++i)
    buf[i] = static_cast<std::byte>(i);

  const struct {
    std::size_t size;
    std::uint64_t hash;
  } vectors[] = {
      {0, 0xEF46DB3751D8E999},   {1, 0xE934A84ADB052768},
      {4, 0xFFCED8604453CC1E},   {8, 0x884A173614B81B8D},
      {14, 0x5CDA8B69BBFC1D45},  {31, 0xC346D2B59B4D8EE1},
      {32, 0xCBF59C5116FF32B4},  {63, 0xE26AA9E2A95F8E4F},
      {100, 0x6AC1E58032166597},
  };
  for (const auto &v : vectors)
    CHECK(crunch::xxh64(buf.data(), v.size) == v.hash);
}

// the streaming form must match the hash for any update split
void test_streaming() {
  std::array<std::byte, 100> buf{};
  for (std::size_t i = 0; i < buf.size(); ++i)
    buf[i] = static_cast<std::byte>(i * 7);

  for (const std::size_t size :
       {std::size_t{0}, std::size_t{1}, std::size_t{31}, std::size_t{32},
        std::size_t{33}, std::size_t{100}}) {
    const std::uint64_t expect = crunch::xxh64(buf.data(), size);
    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{100}}) {
      crunch::xxh64_state state;
      for (std::size_t i = 0; i < size; i += chunk) {
        const std::size_t take = size - i < chunk ? size - i : chunk;
        state.update(buf.data() + i, take);
      }
      CHECK(state.digest() == expect);
    }
  }
}

// frames store the low 4 bytes of the content hash as the checksum (3.1.1)
void test_content_checksum() {
  const auto content = test::read_file(data_dir + "/hello.txt");
  CHECK(content.size() == 50);
  if (content.size() != 50)
    return;
  const std::uint64_t hash = crunch::xxh64(content.data(), content.size());
  CHECK(hash == 0xCE3F1F7E847D1AEB);
  CHECK(static_cast<std::uint32_t>(hash) == 0x847D1AEB);
}

} // namespace

int main(int argc, char **argv) {
  data_dir = argc > 1 ? argv[1] : "tests/data";
  test_vectors();
  test_streaming();
  test_content_checksum();
  return test::report("xxhash");
}
