#pragma once

// Zstandard format definitions from RFC 8878,
// https://www.rfc-editor.org/rfc/rfc8878
// Comments reference its section and table numbers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace crunch {

inline constexpr std::size_t table_alignment = 64;

// 3.1.1 Zstandard frames

inline constexpr std::uint32_t frame_magic = 0xFD2FB528;
inline constexpr std::uint32_t dictionary_magic = 0xEC30A437;
inline constexpr std::uint32_t skippable_magic_min = 0x184D2A50;
inline constexpr std::uint32_t skippable_magic_max = 0x184D2A5F;

inline constexpr std::size_t frame_header_size_min = 2;
inline constexpr std::size_t frame_header_size_max = 14;

inline constexpr std::uint64_t window_size_min = 1024;
inline constexpr std::uint64_t window_size_max =
    (std::uint64_t{1} << 41) + 7 * (std::uint64_t{1} << 38);
// 3.1.1.1.2 recommends decoders support windows up to at least 8 MB
inline constexpr std::uint64_t window_size_recommended_max = 8 * 1024 * 1024;

// Frame_Header_Descriptor, Tables 3-5
struct frame_header_descriptor {
  std::uint8_t raw = 0;

  constexpr unsigned frame_content_size_flag() const { return raw >> 6; }
  constexpr bool single_segment() const { return (raw & 0x20) != 0; }
  constexpr bool reserved_bit() const { return (raw & 0x08) != 0; }
  constexpr bool content_checksum() const { return (raw & 0x04) != 0; }
  constexpr unsigned dictionary_id_flag() const { return raw & 0x03; }

  // stored Frame_Content_Size is offset by 256 when this returns 2
  constexpr unsigned fcs_field_size() const {
    const unsigned flag = frame_content_size_flag();
    if (flag == 0)
      return single_segment() ? 1u : 0u;
    return 1u << flag;
  }

  constexpr unsigned did_field_size() const {
    return dictionary_id_flag() == 3 ? 4u : dictionary_id_flag();
  }
};

// Window_Descriptor, 3.1.1.1.2
struct window_descriptor {
  std::uint8_t raw = 0;

  constexpr std::uint64_t window_size() const {
    const std::uint64_t base = std::uint64_t{1} << (10 + (raw >> 3));
    return base + (base / 8) * (raw & 0x07);
  }
};

struct frame_header {
  std::uint64_t window_size = 0;
  std::optional<std::uint64_t> content_size;
  std::uint32_t dictionary_id = 0; // 0 means none
  bool single_segment = false;
  bool has_checksum = false; // low 4 bytes of XXH64(data, seed 0)
};

// 3.1.2 Skippable frames
struct skippable_frame_header {
  std::uint32_t magic = 0;
  std::uint32_t frame_size = 0; // size of the User_Data that follows
};

// 3.1.1.2 Blocks

inline constexpr std::size_t block_header_size = 3;
// Block_Maximum_Size = min(Window_Size, 128 KB), 3.1.1.2.4
inline constexpr std::size_t block_size_cap = 128 * 1024;

enum class block_type : std::uint8_t {
  raw = 0,
  rle = 1,
  compressed = 2,
  reserved = 3, // must be rejected
};

// Block_Header, Table 9
struct block_header {
  bool last_block = false;
  block_type type = block_type::raw;
  std::uint32_t block_size = 0; // 21 bits; repeat count for rle blocks
};

// 3.1.1.3.1 Literals section

enum class literals_block_type : std::uint8_t {
  raw = 0,
  rle = 1,
  compressed = 2,
  treeless = 3, // reuses the previous Huffman tree or the dictionary's
};

// Literals_Section_Header, Tables 12-13
struct literals_section_header {
  literals_block_type type = literals_block_type::raw;
  std::uint32_t regenerated_size = 0;
  std::uint32_t compressed_size = 0; // compressed and treeless only
  std::uint8_t stream_count = 1;     // 1 or 4
  std::uint8_t header_size = 0;      // 1 to 5 bytes
};

// Jump_Table, 3.1.1.3.1.6: sizes of streams 1-3, Stream_4 is derived
inline constexpr std::size_t jump_table_size = 6;

// 3.1.1.3.2 Sequences section

// Symbol_Compression_Modes, Table 15
enum class compression_mode : std::uint8_t {
  predefined = 0,
  rle = 1,
  fse = 2,
  repeat = 3,
};

struct sequences_section_header {
  std::uint32_t sequence_count = 0;
  compression_mode literals_lengths_mode = compression_mode::predefined;
  compression_mode offsets_mode = compression_mode::predefined;
  compression_mode match_lengths_mode = compression_mode::predefined;
  std::uint8_t header_size = 0; // count bytes plus the modes byte
};

struct sequence {
  std::uint32_t literals_length = 0;
  std::uint32_t match_length = 0;
  std::uint32_t offset = 0;
};

// 3.1.1.5, most recent first
struct repeat_offsets {
  std::array<std::uint32_t, 3> value{{1, 4, 8}};
};

// Sequence codes, 3.1.1.3.2.1.1: value = baseline + num_bits raw bits
struct sequence_code {
  std::uint32_t baseline;
  std::uint8_t num_bits;
};

inline constexpr std::size_t literals_length_code_count = 36;
inline constexpr std::size_t match_length_code_count = 53;
// offset codes have no table: offset_value = (1 << code) + code raw bits
inline constexpr unsigned offset_code_supported_max = 31;

// Table 16
inline constexpr std::array<sequence_code, literals_length_code_count>
    literals_length_codes{{
        {0, 0},      {1, 0},     {2, 0},     {3, 0},      {4, 0},
        {5, 0},      {6, 0},     {7, 0},     {8, 0},      {9, 0},
        {10, 0},     {11, 0},    {12, 0},    {13, 0},     {14, 0},
        {15, 0},     {16, 1},    {18, 1},    {20, 1},     {22, 1},
        {24, 2},     {28, 2},    {32, 3},    {40, 3},     {48, 4},
        {64, 6},     {128, 7},   {256, 8},   {512, 9},    {1024, 10},
        {2048, 11},  {4096, 12}, {8192, 13}, {16384, 14}, {32768, 15},
        {65536, 16},
    }};

// Table 17
inline constexpr std::array<sequence_code, match_length_code_count>
    match_length_codes{{
        {3, 0},      {4, 0},      {5, 0},      {6, 0},     {7, 0},
        {8, 0},      {9, 0},      {10, 0},     {11, 0},    {12, 0},
        {13, 0},     {14, 0},     {15, 0},     {16, 0},    {17, 0},
        {18, 0},     {19, 0},     {20, 0},     {21, 0},    {22, 0},
        {23, 0},     {24, 0},     {25, 0},     {26, 0},    {27, 0},
        {28, 0},     {29, 0},     {30, 0},     {31, 0},    {32, 0},
        {33, 0},     {34, 0},     {35, 1},     {37, 1},    {39, 1},
        {41, 1},     {43, 2},     {47, 2},     {51, 3},    {59, 3},
        {67, 4},     {83, 4},     {99, 5},     {131, 7},   {259, 8},
        {515, 9},    {1027, 10},  {2051, 11},  {4099, 12}, {8195, 13},
        {16387, 14}, {32771, 15}, {65539, 16},
    }};

// Default distributions, 3.1.1.3.2.2 (-1 marks a low-probability symbol)

inline constexpr unsigned literals_length_default_accuracy_log = 6;
inline constexpr std::array<std::int16_t, literals_length_code_count>
    literals_length_default_distribution{
        4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1,  1,  2,  2,
        2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};

inline constexpr unsigned match_length_default_accuracy_log = 6;
inline constexpr std::array<std::int16_t, match_length_code_count>
    match_length_default_distribution{
        1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1,  1,  1,  1,  1,  1,  1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};

// only covers offset codes 0-28; larger offsets need an explicit table
inline constexpr unsigned offset_default_accuracy_log = 5;
inline constexpr std::array<std::int16_t, 29> offset_default_distribution{
    1, 1, 1, 1, 1, 1, 2, 2, 2, 1,  1,  1,  1,  1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

// 4.1 FSE

inline constexpr unsigned fse_accuracy_log_min = 5;
inline constexpr unsigned fse_accuracy_log_max = 9;

// per-table limits, 3.1.1.3.2.1 and 4.2.1.2
inline constexpr unsigned literals_length_accuracy_log_max = 9;
inline constexpr unsigned match_length_accuracy_log_max = 9;
inline constexpr unsigned offset_accuracy_log_max = 8;
inline constexpr unsigned huffman_weights_accuracy_log_max = 6;

struct fse_entry {
  std::uint16_t baseline = 0;
  std::uint8_t num_bits = 0;
  std::uint8_t symbol = 0;
};

struct fse_table {
  alignas(table_alignment)
      std::array<fse_entry, std::size_t{1} << fse_accuracy_log_max> entries{};
  std::uint8_t accuracy_log = 0;

  constexpr std::size_t size() const { return std::size_t{1} << accuracy_log; }
};

// 4.2 Huffman coding

inline constexpr unsigned huffman_code_length_max = 11;

struct huffman_entry {
  std::uint8_t symbol = 0;
  std::uint8_t num_bits = 0;
};

struct huffman_table {
  alignas(table_alignment) std::array<
      huffman_entry, std::size_t{1} << huffman_code_length_max> entries{};
  std::uint8_t max_num_bits = 0;

  constexpr std::size_t size() const { return std::size_t{1} << max_num_bits; }
};

// 5 Dictionary format
struct dictionary {
  std::uint32_t id = 0;
  std::optional<huffman_table> literals_huffman;
  std::optional<fse_table> offset_table;
  std::optional<fse_table> match_length_table;
  std::optional<fse_table> literals_length_table;
  repeat_offsets recent_offsets; // each entry must be < content_size
  const std::byte *content = nullptr;
  std::size_t content_size = 0;
};

} // namespace crunch
