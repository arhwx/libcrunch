#include "check.hpp"

#include <crunch/bitstream.hpp>

using test::bytes;

namespace {

void test_le_reads() {
  const unsigned char raw[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x88};
  const std::byte *p = bytes(raw);
  CHECK(crunch::read_le16(p) == 0x0201);
  CHECK(crunch::read_le24(p) == 0x030201);
  CHECK(crunch::read_le32(p) == 0x04030201);
  CHECK(crunch::read_le64(p) == 0x8807060504030201ull);
}

void test_bit_reader() {
  // 0x47 = 0100 0111 read from the top; 0x01 is only the sentinel
  const unsigned char raw[] = {0x47, 0x01};
  auto r = crunch::bit_reader::from_end(bytes(raw), sizeof(raw));
  CHECK(r);
  auto &br = *r;
  CHECK(br.bits_left() == 8);
  CHECK(br.read(3) == 0b010);
  CHECK(br.read(5) == 0b00111);
  CHECK(br.bits_left() == 0);
  CHECK(!br.overflowed());
  CHECK(br.read(1) == 0);
  CHECK(br.overflowed());

  // sentinel inside the last byte: 0x25 = 0010 0101 leaves 5 data bits
  const unsigned char raw2[] = {0xb4, 0x25};
  auto r2 = crunch::bit_reader::from_end(bytes(raw2), sizeof(raw2));
  CHECK(r2);
  auto &br2 = *r2;
  CHECK(br2.bits_left() == 13);
  CHECK(br2.read(5) == 0b00101);
  CHECK(br2.read(8) == 0xb4);
  CHECK(!br2.overflowed());

  // reads spanning byte boundaries
  const unsigned char raw3[] = {0xff, 0x0f, 0x01};
  auto r3 = crunch::bit_reader::from_end(bytes(raw3), sizeof(raw3));
  CHECK(r3);
  auto &br3 = *r3;
  CHECK(br3.bits_left() == 16);
  CHECK(br3.read(12) == 0x0ff);
  CHECK(br3.read(4) == 0xf);

  const unsigned char no_sentinel[] = {0x47, 0x00};
  auto bad = crunch::bit_reader::from_end(bytes(no_sentinel), 2);
  CHECK(!bad);
  CHECK(bad.err() == crunch::error::corrupt_bitstream);
  CHECK(!crunch::bit_reader::from_end(bytes(no_sentinel), 0));
}

} // namespace

int main() {
  test_le_reads();
  test_bit_reader();
  return test::report("bitstream");
}
