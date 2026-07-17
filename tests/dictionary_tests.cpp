#include "check.hpp"

#include <crunch/dictionary.hpp>

#include <cstring>

using test::bytes;

namespace {

std::string data_dir;

// magic, id 3000, the spec-example huffman tree, three {16, 16}
// distributions, repeat offsets {2, 4, 6}, then 6 content bytes
const unsigned char small_dict[] = {
    0x37, 0xa4, 0x30, 0xec, 0xb8, 0x0b, 0x00, 0x00, 0x84, 0x43, 0x20, 0x10,
    0x10, 0x3f, 0x10, 0x3f, 0x10, 0x3f, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 'a',  'b',  'c',  'd',  'e',  'f'};

void test_formatted() {
  crunch::dictionary dict;
  CHECK(crunch::parse_dictionary(bytes(small_dict), sizeof(small_dict), dict) ==
        crunch::error::none);
  CHECK(dict.id == 3000);
  CHECK(dict.literals_huffman.has_value());
  CHECK(dict.literals_huffman->max_num_bits == 4);
  CHECK(dict.offset_table.has_value());
  CHECK(dict.offset_table->accuracy_log == 5);
  CHECK(dict.match_length_table.has_value());
  CHECK(dict.literals_length_table.has_value());
  CHECK(dict.recent_offsets.value[0] == 2);
  CHECK(dict.recent_offsets.value[1] == 4);
  CHECK(dict.recent_offsets.value[2] == 6);
  CHECK(dict.content_size == 6);
  CHECK(std::memcmp(dict.content, "abcdef", 6) == 0);
}

void test_raw_content() {
  const unsigned char raw[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                               0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
  crunch::dictionary dict;
  CHECK(crunch::parse_dictionary(bytes(raw), sizeof(raw), dict) ==
        crunch::error::none);
  CHECK(dict.id == 0);
  CHECK(!dict.literals_huffman.has_value());
  CHECK(!dict.offset_table.has_value());
  CHECK(dict.content == bytes(raw));
  CHECK(dict.content_size == sizeof(raw));
  CHECK(dict.recent_offsets.value[0] == 1);
  CHECK(dict.recent_offsets.value[1] == 4);
  CHECK(dict.recent_offsets.value[2] == 8);
}

void test_bad_dictionaries() {
  crunch::dictionary dict;

  // below the 8 byte minimum
  const unsigned char tiny[] = {0x01, 0x02, 0x03};
  CHECK(crunch::parse_dictionary(bytes(tiny), sizeof(tiny), dict) ==
        crunch::error::bad_dictionary);

  unsigned char patched[sizeof(small_dict)];

  // a zero repeat offset
  std::memcpy(patched, small_dict, sizeof(small_dict));
  patched[18] = 0x00;
  CHECK(crunch::parse_dictionary(bytes(patched), sizeof(patched), dict) ==
        crunch::error::bad_dictionary);

  // a repeat offset beyond the content
  std::memcpy(patched, small_dict, sizeof(small_dict));
  patched[26] = 0x07;
  CHECK(crunch::parse_dictionary(bytes(patched), sizeof(patched), dict) ==
        crunch::error::bad_dictionary);

  // cut off inside the repeat offsets
  CHECK(crunch::parse_dictionary(bytes(small_dict), 24, dict) ==
        crunch::error::truncated_input);

  // cut off inside the entropy tables
  CHECK(crunch::parse_dictionary(bytes(small_dict), 13, dict) ==
        crunch::error::truncated_input);
}

void test_fixture() {
  const auto file = test::read_file(data_dir + "/hello.dict");
  const auto content = test::read_file(data_dir + "/hello.txt");
  CHECK(file.size() == 80);
  CHECK(content.size() == 50);
  if (file.size() != 80 || content.size() != 50)
    return;

  crunch::dictionary dict;
  CHECK(crunch::parse_dictionary(file.data(), file.size(), dict) ==
        crunch::error::none);
  CHECK(dict.id == 3000);
  CHECK(dict.content_size == 50);
  CHECK(std::memcmp(dict.content, content.data(), content.size()) == 0);
}

} // namespace

int main(int argc, char **argv) {
  data_dir = argc > 1 ? argv[1] : "tests/data";
  test_formatted();
  test_raw_content();
  test_bad_dictionaries();
  test_fixture();
  return test::report("dictionary");
}
