#include <crunch/bitstream.hpp>
#include <crunch/huffman.hpp>
#include <crunch/literals.hpp>

#include <cstring>

namespace crunch {

namespace {

// one backward stream regenerating an exact number of literals; it must
// be entirely and exactly consumed (4.2.2)
error decode_stream(const huffman_table &table, const std::byte *data,
                    std::size_t size, std::byte *dst, std::size_t count) {
  auto stream = bit_reader::from_end(data, size);
  if (!stream)
    return error::corrupt_bitstream;
  bit_reader bits = *stream;
  for (std::size_t i = 0; i < count; ++i) {
    const huffman_entry &entry = table.entries[bits.peek(table.max_num_bits)];
    bits.advance(entry.num_bits);
    dst[i] = std::byte{entry.symbol};
  }
  if (bits.overflowed() || bits.bits_left() != 0)
    return error::corrupt_bitstream;
  return error::none;
}

} // namespace

result<literals_section_header>
parse_literals_section_header(const std::byte *data, std::size_t size) {
  if (size == 0)
    return error::truncated_input;
  const unsigned first = std::to_integer<unsigned>(data[0]);
  literals_section_header header;
  header.type = static_cast<literals_block_type>(first & 0x3);
  const unsigned size_format = (first >> 2) & 0x3;

  if (header.type == literals_block_type::raw ||
      header.type == literals_block_type::rle) {
    // 1, 2 or 3 header bytes, Table 12
    switch (size_format) {
    case 0:
    case 2:
      header.header_size = 1;
      header.regenerated_size = first >> 3;
      return header;
    case 1:
      header.header_size = 2;
      if (size < 2)
        return error::truncated_input;
      header.regenerated_size = read_le16(data) >> 4;
      return header;
    default:
      header.header_size = 3;
      if (size < 3)
        return error::truncated_input;
      header.regenerated_size = read_le24(data) >> 4;
      return header;
    }
  }

  // compressed and treeless carry both sizes and a stream count, Table 13
  unsigned field_bits = 10;
  header.header_size = 3;
  header.stream_count = size_format == 0 ? 1 : 4;
  if (size_format == 2) {
    header.header_size = 4;
    field_bits = 14;
  } else if (size_format == 3) {
    header.header_size = 5;
    field_bits = 18;
  }
  if (size < header.header_size)
    return error::truncated_input;

  std::uint64_t value = 0;
  for (unsigned i = 0; i < header.header_size; ++i)
    value |= std::to_integer<std::uint64_t>(data[i]) << (8 * i);
  const std::uint32_t mask = (1u << field_bits) - 1;
  header.regenerated_size = static_cast<std::uint32_t>(value >> 4) & mask;
  header.compressed_size =
      static_cast<std::uint32_t>(value >> (4 + field_bits)) & mask;
  return header;
}

error decode_literals(const literals_section_header &header,
                      const std::byte *data, std::size_t size,
                      huffman_table &table, bool &have_table, std::byte *dst,
                      std::size_t dst_size) {
  if (dst_size < header.regenerated_size)
    return error::output_too_small;
  if (size < header.header_size)
    return error::truncated_input;
  const std::byte *payload = data + header.header_size;
  std::size_t remaining = size - header.header_size;

  switch (header.type) {
  case literals_block_type::raw:
    if (remaining < header.regenerated_size)
      return error::truncated_input;
    std::memcpy(dst, payload, header.regenerated_size);
    return error::none;
  case literals_block_type::rle:
    if (remaining < 1)
      return error::truncated_input;
    std::memset(dst, std::to_integer<int>(payload[0]), header.regenerated_size);
    return error::none;
  default:
    break;
  }

  if (remaining < header.compressed_size)
    return error::truncated_input;
  remaining = header.compressed_size;

  if (header.type == literals_block_type::compressed) {
    auto weights = parse_huffman_weights(payload, remaining);
    if (!weights)
      return weights.err();
    const error err = build_huffman_table(weights->weights.data(),
                                          weights->symbol_count, table);
    if (err != error::none)
      return err;
    have_table = true;
    payload += weights->consumed;
    remaining -= weights->consumed;
  } else if (!have_table) {
    return error::missing_table;
  }

  if (header.stream_count == 1)
    return decode_stream(table, payload, remaining, dst,
                         header.regenerated_size);

  // four streams behind a jump table; streams 1-3 regenerate a rounded-up
  // quarter each and stream 4 the remainder (3.1.1.3.1.6)
  if (remaining < jump_table_size)
    return error::corrupt_bitstream;
  const std::size_t quarter = (header.regenerated_size + 3) / 4;
  if (quarter * 3 > header.regenerated_size)
    return error::corrupt_bitstream;

  std::size_t sizes[4];
  sizes[0] = read_le16(payload);
  sizes[1] = read_le16(payload + 2);
  sizes[2] = read_le16(payload + 4);
  payload += jump_table_size;
  remaining -= jump_table_size;
  if (sizes[0] + sizes[1] + sizes[2] > remaining)
    return error::corrupt_bitstream;
  sizes[3] = remaining - sizes[0] - sizes[1] - sizes[2];

  for (unsigned i = 0; i < 4; ++i) {
    const std::size_t count =
        i == 3 ? header.regenerated_size - 3 * quarter : quarter;
    const error err = decode_stream(table, payload, sizes[i], dst, count);
    if (err != error::none)
      return err;
    payload += sizes[i];
    dst += count;
  }
  return error::none;
}

} // namespace crunch
