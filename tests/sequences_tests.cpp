#include "check.hpp"

#include <crunch/sequences.hpp>

#include <cstring>
using test::bytes;

namespace {

void check_sequence(const crunch::sequence &seq, std::uint32_t literals,
                    std::uint32_t match, std::uint32_t offset_value) {
  CHECK(seq.literals_length == literals);
  CHECK(seq.match_length == match);
  CHECK(seq.offset == offset_value);
}

void test_headers() {
  // no sequences: single byte, no modes
  const unsigned char none[] = {0x00};
  auto r = crunch::parse_sequences_section_header(bytes(none), sizeof(none));
  CHECK(r);
  CHECK(r->sequence_count == 0);
  CHECK(r->header_size == 1);

  const unsigned char one[] = {0x54, 0x00};
  auto r1 = crunch::parse_sequences_section_header(bytes(one), sizeof(one));
  CHECK(r1);
  CHECK(r1->sequence_count == 84);
  CHECK(r1->header_size == 2);

  const unsigned char two[] = {0x81, 0x42, 0x00};
  auto r2 = crunch::parse_sequences_section_header(bytes(two), sizeof(two));
  CHECK(r2);
  CHECK(r2->sequence_count == 322);
  CHECK(r2->header_size == 3);

  const unsigned char three[] = {0xff, 0x34, 0x12, 0x00};
  auto r3 = crunch::parse_sequences_section_header(bytes(three), sizeof(three));
  CHECK(r3);
  CHECK(r3->sequence_count == 37172);
  CHECK(r3->header_size == 4);

  const unsigned char modes[] = {0x01, 0x9c};
  auto r4 = crunch::parse_sequences_section_header(bytes(modes), sizeof(modes));
  CHECK(r4);
  CHECK(r4->literals_lengths_mode == crunch::compression_mode::fse);
  CHECK(r4->offsets_mode == crunch::compression_mode::rle);
  CHECK(r4->match_lengths_mode == crunch::compression_mode::repeat);

  const unsigned char reserved[] = {0x01, 0x01};
  CHECK(
      crunch::parse_sequences_section_header(bytes(reserved), sizeof(reserved))
          .err() == crunch::error::reserved_bit_set);

  CHECK(crunch::parse_sequences_section_header(nullptr, 0).err() ==
        crunch::error::truncated_input);
  CHECK(crunch::parse_sequences_section_header(bytes(two), 1).err() ==
        crunch::error::truncated_input);
  CHECK(crunch::parse_sequences_section_header(bytes(one), 1).err() ==
        crunch::error::truncated_input);
}

// all-zero initial states of the predefined tables give codes 0: literals
// length 0, match length 3, offset value 1
void test_predefined_single() {
  const unsigned char section[] = {0x01, 0x00, 0x00, 0x00, 0x02};
  auto header =
      crunch::parse_sequences_section_header(bytes(section), sizeof(section));
  CHECK(header);
  if (!header)
    return;
  CHECK(header->sequence_count == 1);

  crunch::sequence_tables tables;
  crunch::sequence seq;
  CHECK(crunch::decode_sequences(*header, bytes(section), sizeof(section),
                                 tables, &seq) == crunch::error::none);
  CHECK(tables.valid);
  check_sequence(seq, 0, 3, 1);
}

// two sequences with extra bits and all three state updates; expected
// values come from the appendix A tables
void test_predefined_pair() {
  const unsigned char section[] = {0x02, 0x00, 0x6a, 0x09,
                                   0xa8, 0xc1, 0x0a, 0x50};
  auto header =
      crunch::parse_sequences_section_header(bytes(section), sizeof(section));
  CHECK(header);
  if (!header)
    return;

  crunch::sequence_tables tables;
  crunch::sequence seqs[2];
  CHECK(crunch::decode_sequences(*header, bytes(section), sizeof(section),
                                 tables, seqs) == crunch::error::none);
  check_sequence(seqs[0], 53, 4, 67);
  check_sequence(seqs[1], 0, 61, 13);
}

// rle tables read no state bits, only the offset extra bits remain
void test_rle_modes() {
  const unsigned char section[] = {0x02, 0x54, 0x04, 0x02, 0x00, 0x1d};
  auto header =
      crunch::parse_sequences_section_header(bytes(section), sizeof(section));
  CHECK(header);
  if (!header)
    return;

  crunch::sequence_tables tables;
  crunch::sequence seqs[2];
  CHECK(crunch::decode_sequences(*header, bytes(section), sizeof(section),
                                 tables, seqs) == crunch::error::none);
  check_sequence(seqs[0], 4, 3, 7);
  check_sequence(seqs[1], 4, 3, 5);

  // repeat mode keeps the rle tables
  const unsigned char again[] = {0x01, 0xfc, 0x06};
  auto repeat =
      crunch::parse_sequences_section_header(bytes(again), sizeof(again));
  CHECK(repeat);
  if (!repeat)
    return;
  crunch::sequence seq;
  CHECK(crunch::decode_sequences(*repeat, bytes(again), sizeof(again), tables,
                                 &seq) == crunch::error::none);
  check_sequence(seq, 4, 3, 6);

  // an rle symbol beyond the code range
  const unsigned char bad[] = {0x01, 0x54, 0x00, 0x00, 0x35, 0x01};
  auto bad_header =
      crunch::parse_sequences_section_header(bytes(bad), sizeof(bad));
  CHECK(bad_header);
  if (!bad_header)
    return;
  CHECK(crunch::decode_sequences(*bad_header, bytes(bad), sizeof(bad), tables,
                                 &seq) == crunch::error::bad_distribution);
}

// an explicit distribution equal to the literals length default must
// decode exactly like the predefined table
void test_fse_mode() {
  const unsigned char section[] = {0x01, 0x80, 0x51, 0x10, 0x63, 0x8c, 0x31,
                                   0xc6, 0x18, 0x63, 0x0c, 0x21, 0xc4, 0x18,
                                   0x63, 0x66, 0x66, 0x86, 0x46, 0x92, 0x04,
                                   0x00, 0x00, 0x00, 0x02};
  auto header =
      crunch::parse_sequences_section_header(bytes(section), sizeof(section));
  CHECK(header);
  if (!header)
    return;

  crunch::sequence_tables tables;
  crunch::sequence seq;
  CHECK(crunch::decode_sequences(*header, bytes(section), sizeof(section),
                                 tables, &seq) == crunch::error::none);
  check_sequence(seq, 0, 3, 1);
}

void test_bad_sections() {
  crunch::sequence_tables fresh;
  crunch::sequence seq;

  // repeat mode with no previous tables in the frame
  const unsigned char repeat[] = {0x01, 0xfc, 0x00, 0x00, 0x02};
  auto header =
      crunch::parse_sequences_section_header(bytes(repeat), sizeof(repeat));
  CHECK(header);
  if (!header)
    return;
  CHECK(crunch::decode_sequences(*header, bytes(repeat), sizeof(repeat), fresh,
                                 &seq) == crunch::error::missing_table);

  // bitstream not entirely consumed
  const unsigned char leftover[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x02};
  auto left_header =
      crunch::parse_sequences_section_header(bytes(leftover), sizeof(leftover));
  CHECK(left_header);
  if (!left_header)
    return;
  CHECK(crunch::decode_sequences(*left_header, bytes(leftover),
                                 sizeof(leftover), fresh,
                                 &seq) == crunch::error::corrupt_bitstream);

  // too few bits for the initial states
  const unsigned char starved[] = {0x01, 0x00, 0x01};
  auto starved_header =
      crunch::parse_sequences_section_header(bytes(starved), sizeof(starved));
  CHECK(starved_header);
  if (!starved_header)
    return;
  CHECK(crunch::decode_sequences(*starved_header, bytes(starved),
                                 sizeof(starved), fresh,
                                 &seq) == crunch::error::corrupt_bitstream);

  // no bitstream at all
  const unsigned char empty[] = {0x01, 0x00};
  auto empty_header =
      crunch::parse_sequences_section_header(bytes(empty), sizeof(empty));
  CHECK(empty_header);
  if (!empty_header)
    return;
  CHECK(crunch::decode_sequences(*empty_header, bytes(empty), sizeof(empty),
                                 fresh,
                                 &seq) == crunch::error::corrupt_bitstream);

  // zero sequences must end the section immediately
  const unsigned char clean[] = {0x00};
  auto clean_header =
      crunch::parse_sequences_section_header(bytes(clean), sizeof(clean));
  CHECK(clean_header);
  if (!clean_header)
    return;
  CHECK(crunch::decode_sequences(*clean_header, bytes(clean), sizeof(clean),
                                 fresh, nullptr) == crunch::error::none);
  const unsigned char trailing[] = {0x00, 0xaa};
  CHECK(crunch::decode_sequences(*clean_header, bytes(trailing),
                                 sizeof(trailing), fresh,
                                 nullptr) == crunch::error::corrupt_bitstream);
}

// literals, an overlapping match, a repeat offset, then leftover literals
void test_execution() {
  const unsigned char lits[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  const crunch::sequence seqs[] = {{3, 3, 4}, {1, 3, 1}};

  crunch::repeat_offsets recent;
  std::byte out[32];
  std::size_t written = 0;
  CHECK(crunch::execute_sequences(seqs, 2, bytes(lits), sizeof(lits), recent,
                                  out, sizeof(out),
                                  written) == crunch::error::none);
  CHECK(written == 12);
  CHECK(std::memcmp(out, "abccccddddef", 12) == 0);
  CHECK(recent.value[0] == 1);
  CHECK(recent.value[1] == 1);
  CHECK(recent.value[2] == 4);
}

// each repeat offset selection rule against a distinct history (3.1.1.5)
void test_repeat_offsets() {
  const unsigned char lits[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  const crunch::sequence seqs[] = {
      {8, 2, 3}, // literals ahead, value 3: rotate offset 5 to the front
      {0, 3, 1}, // no literals, value 1: second entry, swap
      {0, 3, 3}, // no literals, value 3: first entry minus one, insert
  };

  crunch::repeat_offsets recent;
  recent.value = {{2, 3, 5}};
  std::byte out[32];
  std::size_t written = 0;
  CHECK(crunch::execute_sequences(seqs, 3, bytes(lits), sizeof(lits), recent,
                                  out, sizeof(out),
                                  written) == crunch::error::none);
  CHECK(written == 16);
  CHECK(std::memcmp(out, "abcdefghdededddd", 16) == 0);
  CHECK(recent.value[0] == 1);
  CHECK(recent.value[1] == 2);
  CHECK(recent.value[2] == 5);
}

void test_bad_execution() {
  const unsigned char lits[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  std::byte out[32];
  std::size_t written = 0;

  // offset reaches before the start of the output
  {
    const crunch::sequence seq = {0, 3, 7};
    crunch::repeat_offsets recent;
    CHECK(crunch::execute_sequences(&seq, 1, bytes(lits), sizeof(lits), recent,
                                    out, sizeof(out), written) ==
          crunch::error::corrupt_bitstream);
  }

  // value 3 without literals resolves to offset 1 - 1 = 0
  {
    const crunch::sequence seq = {0, 1, 3};
    crunch::repeat_offsets recent;
    written = 0;
    CHECK(crunch::execute_sequences(&seq, 1, bytes(lits), sizeof(lits), recent,
                                    out, sizeof(out), written) ==
          crunch::error::corrupt_bitstream);
  }

  // sequence asks for more literals than the section holds
  {
    const crunch::sequence seq = {9, 3, 4};
    crunch::repeat_offsets recent;
    written = 0;
    CHECK(crunch::execute_sequences(&seq, 1, bytes(lits), sizeof(lits), recent,
                                    out, sizeof(out), written) ==
          crunch::error::corrupt_bitstream);
  }

  // match does not fit the remaining output
  {
    const crunch::sequence seq = {3, 3, 4};
    crunch::repeat_offsets recent;
    written = 0;
    CHECK(crunch::execute_sequences(&seq, 1, bytes(lits), sizeof(lits), recent,
                                    out, 4, written) ==
          crunch::error::output_too_small);
  }
}

} // namespace

int main() {
  test_headers();
  test_predefined_single();
  test_predefined_pair();
  test_rle_modes();
  test_fse_mode();
  test_bad_sections();
  test_execution();
  test_repeat_offsets();
  test_bad_execution();
  return test::report("sequences");
}
