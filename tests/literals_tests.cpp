#include "check.hpp"

#include <crunch/frame.hpp>
#include <crunch/literals.hpp>

#include <cstring>

using test::bytes;

namespace {

std::string data_dir;

// compressed sections below embed the spec-example tree from tables 22-25
// as their description: bytes {0x84, 0x43, 0x20, 0x10}, literals 0-5

void test_headers() {
  // raw, 1-byte form, regenerated size 13
  const unsigned char raw1[] = {0x68};
  auto r = crunch::parse_literals_section_header(bytes(raw1), sizeof(raw1));
  CHECK(r);
  CHECK(r->type == crunch::literals_block_type::raw);
  CHECK(r->header_size == 1);
  CHECK(r->regenerated_size == 13);
  CHECK(r->stream_count == 1);

  // raw, 2-byte form, regenerated size 300
  const unsigned char raw2[] = {0xc4, 0x12};
  auto r2 = crunch::parse_literals_section_header(bytes(raw2), sizeof(raw2));
  CHECK(r2);
  CHECK(r2->type == crunch::literals_block_type::raw);
  CHECK(r2->header_size == 2);
  CHECK(r2->regenerated_size == 300);

  // rle, 3-byte form, regenerated size 70000
  const unsigned char rle3[] = {0x0d, 0x17, 0x11};
  auto r3 = crunch::parse_literals_section_header(bytes(rle3), sizeof(rle3));
  CHECK(r3);
  CHECK(r3->type == crunch::literals_block_type::rle);
  CHECK(r3->header_size == 3);
  CHECK(r3->regenerated_size == 70000);

  // compressed, 4-byte form: 4 streams, 14-bit fields
  const unsigned char comp4[] = {0x0a, 0x71, 0x82, 0xbb};
  auto r4 = crunch::parse_literals_section_header(bytes(comp4), sizeof(comp4));
  CHECK(r4);
  CHECK(r4->type == crunch::literals_block_type::compressed);
  CHECK(r4->header_size == 4);
  CHECK(r4->stream_count == 4);
  CHECK(r4->regenerated_size == 10000);
  CHECK(r4->compressed_size == 12000);

  // treeless, 5-byte form: 18-bit fields
  const unsigned char tree5[] = {0x0f, 0xd4, 0x30, 0xa8, 0x61};
  auto r5 = crunch::parse_literals_section_header(bytes(tree5), sizeof(tree5));
  CHECK(r5);
  CHECK(r5->type == crunch::literals_block_type::treeless);
  CHECK(r5->header_size == 5);
  CHECK(r5->stream_count == 4);
  CHECK(r5->regenerated_size == 200000);
  CHECK(r5->compressed_size == 100000);

  CHECK(crunch::parse_literals_section_header(nullptr, 0).err() ==
        crunch::error::truncated_input);
  CHECK(crunch::parse_literals_section_header(bytes(raw2), 1).err() ==
        crunch::error::truncated_input);
  CHECK(crunch::parse_literals_section_header(bytes(tree5), 4).err() ==
        crunch::error::truncated_input);
}

void test_raw_rle() {
  crunch::huffman_table table;
  bool have_table = false;
  std::byte out[32];

  const unsigned char raw[] = {0x68, 'r', 'a', 'w', ' ', 'l', 'i',
                               't',  'e', 'r', 'a', 'l', 's', '!'};
  auto header = crunch::parse_literals_section_header(bytes(raw), sizeof(raw));
  CHECK(header);
  if (!header)
    return;
  CHECK(crunch::decode_literals(*header, bytes(raw), sizeof(raw), table,
                                have_table, out,
                                sizeof(out)) == crunch::error::none);
  CHECK(std::memcmp(out, "raw literals!", 13) == 0);
  CHECK(!have_table);

  // dst shorter than the regenerated size
  CHECK(crunch::decode_literals(*header, bytes(raw), sizeof(raw), table,
                                have_table, out,
                                5) == crunch::error::output_too_small);

  // 20 copies of 'z'
  const unsigned char rle[] = {0xa1, 0x7a};
  auto rle_header =
      crunch::parse_literals_section_header(bytes(rle), sizeof(rle));
  CHECK(rle_header);
  if (!rle_header)
    return;
  CHECK(rle_header->regenerated_size == 20);
  CHECK(crunch::decode_literals(*rle_header, bytes(rle), sizeof(rle), table,
                                have_table, out,
                                sizeof(out)) == crunch::error::none);
  for (std::size_t i = 0; i < 20; ++i)
    CHECK(out[i] == std::byte{'z'});

  // rle byte missing
  CHECK(crunch::decode_literals(*rle_header, bytes(rle), 1, table, have_table,
                                out,
                                sizeof(out)) == crunch::error::truncated_input);
}

// one stream: tree description, then literals {0, 4, 1, 2, 5}; a treeless
// section then reuses the same table
void test_single_stream() {
  const unsigned char section[] = {0x52, 0x80, 0x01, 0x84, 0x43,
                                   0x20, 0x10, 0x91, 0x60};
  auto header =
      crunch::parse_literals_section_header(bytes(section), sizeof(section));
  CHECK(header);
  if (!header)
    return;
  CHECK(header->type == crunch::literals_block_type::compressed);
  CHECK(header->stream_count == 1);
  CHECK(header->regenerated_size == 5);
  CHECK(header->compressed_size == 6);

  crunch::huffman_table table;
  bool have_table = false;
  std::byte out[8];
  CHECK(crunch::decode_literals(*header, bytes(section), sizeof(section), table,
                                have_table, out,
                                sizeof(out)) == crunch::error::none);
  CHECK(have_table);
  const std::uint8_t want[] = {0, 4, 1, 2, 5};
  for (std::size_t i = 0; i < 5; ++i)
    CHECK(out[i] == std::byte{want[i]});

  const unsigned char treeless[] = {0x53, 0x80, 0x00, 0x91, 0x60};
  auto reuse =
      crunch::parse_literals_section_header(bytes(treeless), sizeof(treeless));
  CHECK(reuse);
  if (!reuse)
    return;
  CHECK(reuse->type == crunch::literals_block_type::treeless);
  CHECK(reuse->compressed_size == 2);
  std::byte again[8];
  CHECK(crunch::decode_literals(*reuse, bytes(treeless), sizeof(treeless),
                                table, have_table, again,
                                sizeof(again)) == crunch::error::none);
  for (std::size_t i = 0; i < 5; ++i)
    CHECK(again[i] == std::byte{want[i]});

  // treeless with no previous table in the frame
  crunch::huffman_table fresh;
  bool no_table = false;
  CHECK(crunch::decode_literals(*reuse, bytes(treeless), sizeof(treeless),
                                fresh, no_table, again,
                                sizeof(again)) == crunch::error::missing_table);
}

// four streams of two literals each behind the jump table
void test_four_streams() {
  const unsigned char section[] = {0x86, 0x80, 0x03, 0x84, 0x43, 0x20,
                                   0x10, 0x01, 0x00, 0x01, 0x00, 0x01,
                                   0x00, 0x30, 0x29, 0x23, 0x0d};
  auto header =
      crunch::parse_literals_section_header(bytes(section), sizeof(section));
  CHECK(header);
  if (!header)
    return;
  CHECK(header->stream_count == 4);
  CHECK(header->regenerated_size == 8);
  CHECK(header->compressed_size == 14);

  crunch::huffman_table table;
  bool have_table = false;
  std::byte out[8];
  CHECK(crunch::decode_literals(*header, bytes(section), sizeof(section), table,
                                have_table, out,
                                sizeof(out)) == crunch::error::none);
  const std::uint8_t want[] = {0, 4, 1, 2, 5, 0, 0, 1};
  for (std::size_t i = 0; i < 8; ++i)
    CHECK(out[i] == std::byte{want[i]});
}

void test_bad_sections() {
  crunch::huffman_table table;
  bool have_table = false;
  std::byte out[16];

  // stream leaves 4 bits unconsumed
  const unsigned char leftover[] = {0x52, 0x80, 0x01, 0x84, 0x43,
                                    0x20, 0x10, 0x91, 0xe0};
  auto header =
      crunch::parse_literals_section_header(bytes(leftover), sizeof(leftover));
  CHECK(header);
  if (!header)
    return;
  CHECK(crunch::decode_literals(*header, bytes(leftover), sizeof(leftover),
                                table, have_table, out, sizeof(out)) ==
        crunch::error::corrupt_bitstream);

  // stream too short for its five literals
  const unsigned char starved[] = {0x52, 0x40, 0x01, 0x84,
                                   0x43, 0x20, 0x10, 0x01};
  auto starved_header =
      crunch::parse_literals_section_header(bytes(starved), sizeof(starved));
  CHECK(starved_header);
  if (!starved_header)
    return;
  CHECK(crunch::decode_literals(
            *starved_header, bytes(starved), sizeof(starved), table, have_table,
            out, sizeof(out)) == crunch::error::corrupt_bitstream);

  // jump table sizes exceed the remaining payload
  const unsigned char bad_jump[] = {0x86, 0x80, 0x03, 0x84, 0x43, 0x20,
                                    0x10, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0x30, 0x29, 0x23, 0x0d};
  auto jump_header =
      crunch::parse_literals_section_header(bytes(bad_jump), sizeof(bad_jump));
  CHECK(jump_header);
  if (!jump_header)
    return;
  CHECK(crunch::decode_literals(*jump_header, bytes(bad_jump), sizeof(bad_jump),
                                table, have_table, out, sizeof(out)) ==
        crunch::error::corrupt_bitstream);

  // regenerated size 2 cannot feed four streams
  const unsigned char tiny[] = {0x26, 0x80, 0x03, 0x84, 0x43, 0x20,
                                0x10, 0x01, 0x00, 0x01, 0x00, 0x01,
                                0x00, 0x30, 0x29, 0x23, 0x0d};
  auto tiny_header =
      crunch::parse_literals_section_header(bytes(tiny), sizeof(tiny));
  CHECK(tiny_header);
  if (!tiny_header)
    return;
  CHECK(crunch::decode_literals(*tiny_header, bytes(tiny), sizeof(tiny), table,
                                have_table, out, sizeof(out)) ==
        crunch::error::corrupt_bitstream);

  // section cut off before compressed_size bytes arrive
  const unsigned char cut[] = {0x52, 0x80, 0x01, 0x84, 0x43};
  auto cut_header =
      crunch::parse_literals_section_header(bytes(cut), sizeof(cut));
  CHECK(cut_header);
  if (!cut_header)
    return;
  CHECK(crunch::decode_literals(*cut_header, bytes(cut), sizeof(cut), table,
                                have_table, out,
                                sizeof(out)) == crunch::error::truncated_input);
}

// the compressed block in hello.zst opens with a raw literals section
void test_hello_literals() {
  const auto frame = test::read_file(data_dir + "/hello.zst");
  CHECK(!frame.empty());
  if (frame.empty())
    return;
  const std::byte *p = frame.data();
  std::size_t consumed = 0;
  auto hdr = crunch::parse_frame_header(p + 4, frame.size() - 4, consumed);
  CHECK(hdr);
  if (!hdr)
    return;
  const std::size_t off = 4 + consumed + crunch::block_header_size;

  auto section =
      crunch::parse_literals_section_header(p + off, frame.size() - off);
  CHECK(section);
  if (!section)
    return;
  CHECK(section->type == crunch::literals_block_type::raw);
  CHECK(section->header_size == 1);
  CHECK(section->regenerated_size == 13);

  crunch::huffman_table table;
  bool have_table = false;
  std::byte out[32];
  CHECK(crunch::decode_literals(*section, p + off, frame.size() - off, table,
                                have_table, out,
                                sizeof(out)) == crunch::error::none);
  CHECK(std::memcmp(out, "hello world, ", 13) == 0);
}

} // namespace

int main(int argc, char **argv) {
  data_dir = argc > 1 ? argv[1] : "tests/data";
  test_headers();
  test_raw_rle();
  test_single_stream();
  test_four_streams();
  test_bad_sections();
  test_hello_literals();
  return test::report("literals");
}
