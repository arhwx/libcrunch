#include "check.hpp"

#include <crunch/fse.hpp>

using test::bytes;

namespace {

struct expected_entry {
  std::uint8_t symbol;
  std::uint8_t num_bits;
  std::uint16_t baseline;
};

// RFC 8878 appendix A.1, {symbol, num_bits, baseline} per state
constexpr expected_entry ll_expected[64] = {
    {0, 4, 0},   {0, 4, 16},  {1, 5, 32},  {3, 5, 0},   {4, 5, 0},
    {6, 5, 0},   {7, 5, 0},   {9, 5, 0},   {10, 5, 0},  {12, 5, 0},
    {14, 6, 0},  {16, 5, 0},  {18, 5, 0},  {19, 5, 0},  {21, 5, 0},
    {22, 5, 0},  {24, 5, 0},  {25, 5, 32}, {26, 5, 0},  {27, 6, 0},
    {29, 6, 0},  {31, 6, 0},  {0, 4, 32},  {1, 4, 0},   {2, 5, 0},
    {4, 5, 32},  {5, 5, 0},   {7, 5, 32},  {8, 5, 0},   {10, 5, 32},
    {11, 5, 0},  {13, 6, 0},  {16, 5, 32}, {17, 5, 0},  {19, 5, 32},
    {20, 5, 0},  {22, 5, 32}, {23, 5, 0},  {25, 4, 0},  {25, 4, 16},
    {26, 5, 32}, {28, 6, 0},  {30, 6, 0},  {0, 4, 48},  {1, 4, 16},
    {2, 5, 32},  {3, 5, 32},  {5, 5, 32},  {6, 5, 32},  {8, 5, 32},
    {9, 5, 32},  {11, 5, 32}, {12, 5, 32}, {15, 6, 0},  {17, 5, 32},
    {18, 5, 32}, {20, 5, 32}, {21, 5, 32}, {23, 5, 32}, {24, 5, 32},
    {35, 6, 0},  {34, 6, 0},  {33, 6, 0},  {32, 6, 0},
};

// RFC 8878 appendix A.2
constexpr expected_entry ml_expected[64] = {
    {0, 6, 0},  {1, 4, 0},  {2, 5, 32}, {3, 5, 0},  {5, 5, 0},  {6, 5, 0},
    {8, 5, 0},  {10, 6, 0}, {13, 6, 0}, {16, 6, 0}, {19, 6, 0}, {22, 6, 0},
    {25, 6, 0}, {28, 6, 0}, {31, 6, 0}, {33, 6, 0}, {35, 6, 0}, {37, 6, 0},
    {39, 6, 0}, {41, 6, 0}, {43, 6, 0}, {45, 6, 0}, {1, 4, 16}, {2, 4, 0},
    {3, 5, 32}, {4, 5, 0},  {6, 5, 32}, {7, 5, 0},  {9, 6, 0},  {12, 6, 0},
    {15, 6, 0}, {18, 6, 0}, {21, 6, 0}, {24, 6, 0}, {27, 6, 0}, {30, 6, 0},
    {32, 6, 0}, {34, 6, 0}, {36, 6, 0}, {38, 6, 0}, {40, 6, 0}, {42, 6, 0},
    {44, 6, 0}, {1, 4, 32}, {1, 4, 48}, {2, 4, 16}, {4, 5, 32}, {5, 5, 32},
    {7, 5, 32}, {8, 5, 32}, {11, 6, 0}, {14, 6, 0}, {17, 6, 0}, {20, 6, 0},
    {23, 6, 0}, {26, 6, 0}, {29, 6, 0}, {52, 6, 0}, {51, 6, 0}, {50, 6, 0},
    {49, 6, 0}, {48, 6, 0}, {47, 6, 0}, {46, 6, 0},
};

// RFC 8878 appendix A.3
constexpr expected_entry of_expected[32] = {
    {0, 5, 0},  {6, 4, 0},  {9, 5, 0},  {15, 5, 0}, {21, 5, 0}, {3, 5, 0},
    {7, 4, 0},  {12, 5, 0}, {18, 5, 0}, {23, 5, 0}, {5, 5, 0},  {8, 4, 0},
    {14, 5, 0}, {20, 5, 0}, {2, 5, 0},  {7, 4, 16}, {11, 5, 0}, {17, 5, 0},
    {22, 5, 0}, {4, 5, 0},  {8, 4, 16}, {13, 5, 0}, {19, 5, 0}, {1, 5, 0},
    {6, 4, 16}, {10, 5, 0}, {16, 5, 0}, {28, 5, 0}, {27, 5, 0}, {26, 5, 0},
    {25, 5, 0}, {24, 5, 0},
};

void check_table(const crunch::fse_table &table, const expected_entry *want,
                 std::size_t count, unsigned accuracy_log) {
  CHECK(table.accuracy_log == accuracy_log);
  CHECK(table.size() == count);
  for (std::size_t i = 0; i < count; ++i) {
    CHECK(table.entries[i].symbol == want[i].symbol);
    CHECK(table.entries[i].num_bits == want[i].num_bits);
    CHECK(table.entries[i].baseline == want[i].baseline);
  }
}

void test_predefined_tables() {
  crunch::fse_table table;

  CHECK(crunch::build_fse_table(
            crunch::literals_length_default_distribution.data(),
            crunch::literals_length_default_distribution.size(),
            crunch::literals_length_default_accuracy_log,
            table) == crunch::error::none);
  check_table(table, ll_expected, 64, 6);

  CHECK(
      crunch::build_fse_table(crunch::match_length_default_distribution.data(),
                              crunch::match_length_default_distribution.size(),
                              crunch::match_length_default_accuracy_log,
                              table) == crunch::error::none);
  check_table(table, ml_expected, 64, 6);

  CHECK(crunch::build_fse_table(crunch::offset_default_distribution.data(),
                                crunch::offset_default_distribution.size(),
                                crunch::offset_default_accuracy_log,
                                table) == crunch::error::none);
  check_table(table, of_expected, 32, 5);
}

// the default literals length distribution encoded per 4.1.1; parsing
// it back and building the table must reproduce appendix A.1
void test_parse_distribution() {
  const unsigned char desc[] = {0x51, 0x10, 0x63, 0x8c, 0x31, 0xc6, 0x18,
                                0x63, 0x0c, 0x21, 0xc4, 0x18, 0x63, 0x66,
                                0x66, 0x86, 0x46, 0x92, 0x04, 0x00};
  auto r = crunch::parse_fse_distribution(
      bytes(desc), sizeof(desc), 36, crunch::literals_length_accuracy_log_max);
  CHECK(r);
  if (!r)
    return;
  const auto &dist = *r;
  CHECK(dist.accuracy_log == 6);
  CHECK(dist.symbol_count == 36);
  CHECK(dist.consumed == sizeof(desc));
  for (std::size_t i = 0; i < 36; ++i)
    CHECK(dist.counts[i] == crunch::literals_length_default_distribution[i]);

  crunch::fse_table table;
  CHECK(crunch::build_fse_table(dist.counts.data(), dist.symbol_count,
                                dist.accuracy_log,
                                table) == crunch::error::none);
  check_table(table, ll_expected, 64, 6);
}

// counts {2, 0, 0, 0, 0, 1, 29}: a zero followed by repeat flags 3 and 0
void test_repeat_flags() {
  const unsigned char desc[] = {0x30, 0xc2, 0x88, 0x0f};
  auto r = crunch::parse_fse_distribution(bytes(desc), sizeof(desc), 53, 9);
  CHECK(r);
  if (!r)
    return;
  const std::int16_t want[] = {2, 0, 0, 0, 0, 1, 29};
  CHECK(r->accuracy_log == 5);
  CHECK(r->symbol_count == 7);
  for (std::size_t i = 0; i < 7; ++i)
    CHECK(r->counts[i] == want[i]);
}

void test_bad_distributions() {
  // accuracy log 20
  const unsigned char high_log[] = {0x0f, 0x00};
  CHECK(crunch::parse_fse_distribution(bytes(high_log), sizeof(high_log), 36, 9)
            .err() == crunch::error::bad_distribution);

  // counts {1, 1, 1, 1, 1, 27} against a 4 symbol cap
  const unsigned char six_symbols[] = {0x20, 0x84, 0x10, 0xe2, 0x03};
  CHECK(crunch::parse_fse_distribution(bytes(six_symbols), sizeof(six_symbols),
                                       6, 9));
  CHECK(crunch::parse_fse_distribution(bytes(six_symbols), sizeof(six_symbols),
                                       4, 9)
            .err() == crunch::error::bad_distribution);

  // one symbol taking the whole table
  const unsigned char single[] = {0xf0, 0x03};
  CHECK(crunch::parse_fse_distribution(bytes(single), sizeof(single), 36, 9)
            .err() == crunch::error::bad_distribution);

  // stream ends before the points are spent
  const unsigned char cut[] = {0x51, 0x10};
  CHECK(crunch::parse_fse_distribution(bytes(cut), sizeof(cut), 36, 9).err() ==
        crunch::error::truncated_input);

  CHECK(crunch::parse_fse_distribution(nullptr, 0, 36, 9).err() ==
        crunch::error::truncated_input);

  // build rejects a distribution that does not sum to the table size
  const std::int16_t short_sum[] = {4, 4, 4};
  crunch::fse_table table;
  CHECK(crunch::build_fse_table(short_sum, 3, 6, table) ==
        crunch::error::bad_distribution);
}

} // namespace

int main() {
  test_predefined_tables();
  test_parse_distribution();
  test_repeat_flags();
  test_bad_distributions();
  return test::report("fse");
}
