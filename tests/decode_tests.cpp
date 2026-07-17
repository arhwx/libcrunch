#include "check.hpp"

#include <crunch/decode.hpp>

#include <cstring>
#include <vector>

using test::bytes;

namespace {

std::string data_dir;

void check_roundtrip(const std::string &name) {
  const auto original = test::read_file(data_dir + "/" + name + ".bin");
  const auto frame = test::read_file(data_dir + "/" + name + ".zst");
  CHECK(!original.empty());
  CHECK(!frame.empty());
  if (original.empty() || frame.empty())
    return;

  std::vector<std::byte> out(original.size());
  std::size_t consumed = 0;
  auto r = crunch::decode_frame(frame.data(), frame.size(), out.data(),
                                out.size(), consumed);
  CHECK(r);
  CHECK(*r == original.size());
  CHECK(consumed == frame.size());
  CHECK(std::memcmp(out.data(), original.data(), original.size()) == 0);
}

void test_raw_block() { check_roundtrip("random"); }

// zstd never emits rle blocks, so rle.zst is hand-built, checked with zstd -d
void test_rle_block() { check_roundtrip("rle"); }

void test_multi_frame() {
  auto stream = test::read_file(data_dir + "/random.zst");
  const auto second = test::read_file(data_dir + "/rle.zst");
  stream.insert(stream.end(), second.begin(), second.end());

  std::vector<std::byte> out(512 + 4096);
  std::size_t off = 0;
  std::size_t written = 0;
  while (off < stream.size()) {
    std::size_t consumed = 0;
    auto r = crunch::decode_frame(stream.data() + off, stream.size() - off,
                                  out.data() + written, out.size() - written,
                                  consumed);
    CHECK(r);
    if (!r)
      return;
    off += consumed;
    written += *r;
  }
  CHECK(written == out.size());
}

void test_skippable_frame() {
  const unsigned char skip[] = {0x50, 0x2a, 0x4d, 0x18, 0x04, 0x00,
                                0x00, 0x00, 0xde, 0xad, 0xbe, 0xef};
  std::size_t consumed = 0;
  auto r =
      crunch::decode_frame(bytes(skip), sizeof(skip), nullptr, 0, consumed);
  CHECK(r);
  CHECK(*r == 0);
  CHECK(consumed == sizeof(skip));

  auto trunc =
      crunch::decode_frame(bytes(skip), sizeof(skip) - 1, nullptr, 0, consumed);
  CHECK(trunc.err() == crunch::error::truncated_input);
}

void test_errors() {
  std::size_t consumed = 0;
  std::vector<std::byte> out(8192);

  const unsigned char junk[] = {0x11, 0x22, 0x33, 0x44};
  CHECK(crunch::decode_frame(bytes(junk), sizeof(junk), out.data(), out.size(),
                             consumed)
            .err() == crunch::error::bad_magic);

  auto frame = test::read_file(data_dir + "/random.zst");
  CHECK(!frame.empty());
  if (frame.empty())
    return;

  CHECK(crunch::decode_frame(frame.data(), frame.size() - 1, out.data(),
                             out.size(), consumed)
            .err() == crunch::error::truncated_input);

  CHECK(crunch::decode_frame(frame.data(), frame.size(), out.data(), 511,
                             consumed)
            .err() == crunch::error::output_too_small);

  frame.back() ^= std::byte{0xFF};
  CHECK(crunch::decode_frame(frame.data(), frame.size(), out.data(), out.size(),
                             consumed)
            .err() == crunch::error::checksum_mismatch);

  // hello.zst holds a compressed block, not implemented yet
  const auto compressed = test::read_file(data_dir + "/hello.zst");
  CHECK(crunch::decode_frame(compressed.data(), compressed.size(), out.data(),
                             out.size(), consumed)
            .err() == crunch::error::unsupported);
}

} // namespace

int main(int argc, char **argv) {
  data_dir = argc > 1 ? argv[1] : "tests/data";
  test_raw_block();
  test_rle_block();
  test_multi_frame();
  test_skippable_frame();
  test_errors();
  return test::report("decode");
}
