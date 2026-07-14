#include "check.hpp"

#include <crunch/bitstream.hpp>
#include <crunch/format.hpp>
#include <crunch/frame.hpp>

using test::bytes;

namespace {

std::string data_dir;

void test_frame_header_forms() {
  std::size_t consumed = 0;

  // smallest form: empty descriptor plus 1 KB window
  const unsigned char minimal[] = {0x00, 0x00};
  auto r = crunch::parse_frame_header(bytes(minimal), sizeof(minimal), consumed);
  CHECK(r);
  CHECK(consumed == 2);
  CHECK(r.value().window_size == 1024);
  CHECK(!r.value().content_size);
  CHECK(r.value().dictionary_id == 0);
  CHECK(!r.value().single_segment);
  CHECK(!r.value().has_checksum);

  // 2-byte content size is stored offset by 256
  const unsigned char fcs2[] = {0x40, 0x58, 0x00, 0x01};
  auto r2 = crunch::parse_frame_header(bytes(fcs2), sizeof(fcs2), consumed);
  CHECK(r2);
  CHECK(consumed == 4);
  CHECK(r2.value().window_size == 2u * 1024 * 1024);
  CHECK(r2.value().content_size == 512u);

  // 2-byte dictionary id
  const unsigned char dict2[] = {0x02, 0x00, 0x34, 0x12};
  auto r3 = crunch::parse_frame_header(bytes(dict2), sizeof(dict2), consumed);
  CHECK(r3);
  CHECK(consumed == 4);
  CHECK(r3.value().dictionary_id == 0x1234);

  // largest form: window byte, 4-byte dictionary id, 8-byte content size
  const unsigned char maximal[] = {0xc3, 0x00, 0x78, 0x56, 0x34, 0x12, 0x08,
                                   0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  auto r4 = crunch::parse_frame_header(bytes(maximal), sizeof(maximal), consumed);
  CHECK(r4);
  CHECK(consumed == crunch::frame_header_size_max);
  CHECK(r4.value().dictionary_id == 0x12345678);
  CHECK(r4.value().content_size == 0x0102030405060708ull);

  const unsigned char reserved[] = {0x08, 0x00};
  auto bad =
      crunch::parse_frame_header(bytes(reserved), sizeof(reserved), consumed);
  CHECK(!bad);
  CHECK(bad.err() == crunch::error::reserved_bit_set);

  const unsigned char short_fcs[] = {0x40, 0x58, 0x00};
  auto trunc =
      crunch::parse_frame_header(bytes(short_fcs), sizeof(short_fcs), consumed);
  CHECK(!trunc);
  CHECK(trunc.err() == crunch::error::truncated_input);
  CHECK(crunch::parse_frame_header(bytes(short_fcs), 0, consumed).err() ==
        crunch::error::truncated_input);
}

void test_block_header() {
  // 0x21 = 0b100001: last, raw, size 4
  const unsigned char raw_blk[] = {0x21, 0x00, 0x00};
  auto r = crunch::parse_block_header(bytes(raw_blk), sizeof(raw_blk));
  CHECK(r);
  CHECK(r.value().last_block);
  CHECK(r.value().type == crunch::block_type::raw);
  CHECK(r.value().block_size == 4);

  // 0x0b = 0b1011: last, rle, size 1
  const unsigned char rle_blk[] = {0x0b, 0x00, 0x00};
  auto r2 = crunch::parse_block_header(bytes(rle_blk), sizeof(rle_blk));
  CHECK(r2);
  CHECK(r2.value().type == crunch::block_type::rle);
  CHECK(r2.value().block_size == 1);

  const unsigned char reserved_blk[] = {0x06, 0x00, 0x00};
  auto bad =
      crunch::parse_block_header(bytes(reserved_blk), sizeof(reserved_blk));
  CHECK(!bad);
  CHECK(bad.err() == crunch::error::reserved_block_type);

  CHECK(crunch::parse_block_header(bytes(raw_blk), 2).err() ==
        crunch::error::truncated_input);
}

void test_magic_dispatch() {
  CHECK(crunch::is_skippable_magic(0x184D2A50));
  CHECK(crunch::is_skippable_magic(0x184D2A5F));
  CHECK(!crunch::is_skippable_magic(0x184D2A60));
  CHECK(!crunch::is_skippable_magic(crunch::frame_magic));
}

// walk both reference-encoder frames block by block and land exactly on
// the checksum; expected values come from the spec and from zstd -l -v
void test_real_frames() {
  {
    // hello.txt compressed as a file: single segment, content size known
    const auto frame = test::read_file(data_dir + "/hello.zst");
    CHECK(frame.size() == 32);
    if (frame.size() != 32)
      return;
    const std::byte *p = frame.data();
    const std::size_t size = frame.size();
    CHECK(crunch::read_le32(p) == crunch::frame_magic);
    std::size_t consumed = 0;
    auto hdr = crunch::parse_frame_header(p + 4, size - 4, consumed);
    CHECK(hdr);
    CHECK(consumed == 2);
    CHECK(hdr.value().single_segment);
    CHECK(hdr.value().has_checksum);
    CHECK(hdr.value().content_size == 50u);
    CHECK(hdr.value().window_size == 50);

    std::size_t off = 4 + consumed;
    auto blk = crunch::parse_block_header(p + off, size - off);
    CHECK(blk);
    CHECK(blk.value().last_block);
    CHECK(blk.value().type == crunch::block_type::compressed);
    CHECK(blk.value().block_size == 19);
    off += crunch::block_header_size + blk.value().block_size;
    CHECK(off + 4 == size);
    CHECK(crunch::read_le32(p + off) == 0x847d1aeb);
  }
  {
    // same input piped: no content size, 2 MiB window descriptor instead
    const auto frame = test::read_file(data_dir + "/hello_piped.zst");
    CHECK(frame.size() == 32);
    if (frame.size() != 32)
      return;
    const std::byte *p = frame.data();
    const std::size_t size = frame.size();
    std::size_t consumed = 0;
    auto hdr = crunch::parse_frame_header(p + 4, size - 4, consumed);
    CHECK(hdr);
    CHECK(consumed == 2);
    CHECK(!hdr.value().single_segment);
    CHECK(!hdr.value().content_size);
    CHECK(hdr.value().has_checksum);
    CHECK(hdr.value().window_size == 2u * 1024 * 1024);
  }
}

} // namespace

int main(int argc, char **argv) {
  data_dir = argc > 1 ? argv[1] : "tests/data";
  test_frame_header_forms();
  test_block_header();
  test_magic_dispatch();
  test_real_frames();
  return test::report("frame");
}
