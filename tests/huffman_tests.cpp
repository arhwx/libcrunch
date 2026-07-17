#include "check.hpp"

#include <crunch/huffman.hpp>

using test::bytes;

namespace {

// spec example, tables 22-25: literals 0-5 with code lengths 1,2,3,0,4,4
void test_direct_weights() {
  const unsigned char desc[] = {0x84, 0x43, 0x20, 0x10};
  auto r = crunch::parse_huffman_weights(bytes(desc), sizeof(desc));
  CHECK(r);
  if (!r)
    return;
  const auto &parsed = *r;
  CHECK(parsed.symbol_count == 6);
  CHECK(parsed.consumed == sizeof(desc));
  const std::uint8_t want[] = {4, 3, 2, 0, 1, 1};
  for (std::size_t i = 0; i < 6; ++i)
    CHECK(parsed.weights[i] == want[i]);

  crunch::huffman_table table;
  CHECK(crunch::build_huffman_table(parsed.weights.data(), parsed.symbol_count,
                                    table) == crunch::error::none);
  CHECK(table.max_num_bits == 4);
  CHECK(table.size() == 16);
  // prefix codes from table 25: 4->0000, 5->0001, 2->001, 1->01, 0->1
  const struct {
    std::size_t first, last;
    std::uint8_t symbol, num_bits;
  } spans[] = {
      {0, 0, 4, 4}, {1, 1, 5, 4}, {2, 3, 2, 3}, {4, 7, 1, 2}, {8, 15, 0, 1},
  };
  for (const auto &span : spans)
    for (std::size_t i = span.first; i <= span.last; ++i) {
      CHECK(table.entries[i].symbol == span.symbol);
      CHECK(table.entries[i].num_bits == span.num_bits);
    }
}

// smallest description: one stored weight, the second symbol is implicit
void test_minimal_direct() {
  const unsigned char desc[] = {0x80, 0x10};
  auto r = crunch::parse_huffman_weights(bytes(desc), sizeof(desc));
  CHECK(r);
  if (!r)
    return;
  CHECK(r->symbol_count == 2);
  CHECK(r->weights[0] == 1);
  CHECK(r->weights[1] == 1);

  crunch::huffman_table table;
  CHECK(crunch::build_huffman_table(r->weights.data(), 2, table) ==
        crunch::error::none);
  CHECK(table.max_num_bits == 1);
  CHECK(table.entries[0].symbol == 0);
  CHECK(table.entries[1].symbol == 1);
}

// distribution {16, 16} at accuracy log 5, then a 12-bit stream decoding
// to weights {1, 1, 0, 1}; the implicit fifth weight completes to 4
void test_fse_weights() {
  const unsigned char desc[] = {0x04, 0x10, 0x3f, 0x91, 0x11};
  auto r = crunch::parse_huffman_weights(bytes(desc), sizeof(desc));
  CHECK(r);
  if (!r)
    return;
  const auto &parsed = *r;
  CHECK(parsed.symbol_count == 5);
  CHECK(parsed.consumed == sizeof(desc));
  const std::uint8_t want[] = {1, 1, 0, 1, 1};
  for (std::size_t i = 0; i < 5; ++i)
    CHECK(parsed.weights[i] == want[i]);

  crunch::huffman_table table;
  CHECK(crunch::build_huffman_table(parsed.weights.data(), parsed.symbol_count,
                                    table) == crunch::error::none);
  CHECK(table.max_num_bits == 2);
  const std::uint8_t symbols[] = {0, 1, 3, 4};
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(table.entries[i].symbol == symbols[i]);
    CHECK(table.entries[i].num_bits == 2);
  }
}

void test_bad_descriptions() {
  CHECK(crunch::parse_huffman_weights(nullptr, 0).err() ==
        crunch::error::truncated_input);

  // direct form cut short: 5 weights need 3 payload bytes
  const unsigned char cut[] = {0x84, 0x43};
  CHECK(crunch::parse_huffman_weights(bytes(cut), sizeof(cut)).err() ==
        crunch::error::truncated_input);

  // fse form cut short: headerByte says 4 bytes follow
  const unsigned char fse_cut[] = {0x04, 0xe0, 0x03};
  CHECK(crunch::parse_huffman_weights(bytes(fse_cut), sizeof(fse_cut)).err() ==
        crunch::error::truncated_input);

  // distribution swallows the whole payload, leaving no bitstream
  const unsigned char no_stream[] = {0x02, 0x10, 0x3f};
  CHECK(crunch::parse_huffman_weights(bytes(no_stream), sizeof(no_stream))
            .err() == crunch::error::truncated_input);

  // weight 12 exceeds the 11-bit code length limit
  const unsigned char big_weight[] = {0x80, 0xc0};
  CHECK(crunch::parse_huffman_weights(bytes(big_weight), sizeof(big_weight))
            .err() == crunch::error::bad_weights);

  // weights {2, 2, 1} sum to 5, which cannot complete to a power of 2
  const unsigned char bad_sum[] = {0x82, 0x22, 0x10};
  CHECK(crunch::parse_huffman_weights(bytes(bad_sum), sizeof(bad_sum)).err() ==
        crunch::error::bad_weights);

  // weights {11, 11} force a 12-bit tree depth
  const unsigned char deep[] = {0x81, 0xbb};
  CHECK(crunch::parse_huffman_weights(bytes(deep), sizeof(deep)).err() ==
        crunch::error::bad_weights);

  // every stored weight is zero
  const unsigned char zeros[] = {0x81, 0x00};
  CHECK(crunch::parse_huffman_weights(bytes(zeros), sizeof(zeros)).err() ==
        crunch::error::bad_weights);

  // distribution {31, -1} decodes only zero weights before the stream ends
  const unsigned char fse_zeros[] = {0x04, 0xe0, 0x03, 0x21, 0x04};
  CHECK(crunch::parse_huffman_weights(bytes(fse_zeros), sizeof(fse_zeros))
            .err() == crunch::error::bad_weights);

  // a 125-byte stream of one-bits never overflows before the 255 cap
  unsigned char runaway[128];
  runaway[0] = 0x7f;
  runaway[1] = 0x10;
  runaway[2] = 0x3f;
  for (std::size_t i = 3; i < sizeof(runaway); ++i)
    runaway[i] = 0xff;
  CHECK(crunch::parse_huffman_weights(bytes(runaway), sizeof(runaway)).err() ==
        crunch::error::bad_weights);
}

void test_bad_build() {
  crunch::huffman_table table;

  // sum 3 is not a power of 2
  const std::uint8_t bad_sum[] = {1, 1, 1};
  CHECK(crunch::build_huffman_table(bad_sum, 3, table) ==
        crunch::error::bad_weights);

  // single nonzero weight
  const std::uint8_t single[] = {2, 0};
  CHECK(crunch::build_huffman_table(single, 2, table) ==
        crunch::error::bad_weights);

  // weight over the limit
  const std::uint8_t deep[] = {12, 1};
  CHECK(crunch::build_huffman_table(deep, 2, table) ==
        crunch::error::bad_weights);

  const std::uint8_t one = 1;
  CHECK(crunch::build_huffman_table(&one, 1, table) ==
        crunch::error::bad_weights);
}

} // namespace

int main() {
  test_direct_weights();
  test_minimal_direct();
  test_fse_weights();
  test_bad_descriptions();
  test_bad_build();
  return test::report("huffman");
}
