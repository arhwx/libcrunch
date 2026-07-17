#include <crunch/bitstream.hpp>
#include <crunch/fse.hpp>
#include <crunch/huffman.hpp>

#include <utility>

namespace crunch {

namespace {

unsigned bit_length(std::uint32_t v) {
  unsigned n = 0;
  while (v != 0) {
    ++n;
    v >>= 1;
  }
  return n;
}

// the last weight completes the total to the nearest power of 2, which
// also sets the tree depth (4.2.1)
error derive_last_weight(huffman_weights &parsed, std::size_t stored) {
  std::uint32_t total = 0;
  for (std::size_t i = 0; i < stored; ++i) {
    if (parsed.weights[i] > huffman_code_length_max)
      return error::bad_weights;
    if (parsed.weights[i] != 0)
      total += 1u << (parsed.weights[i] - 1);
  }
  if (total == 0)
    return error::bad_weights;

  const unsigned max_bits = bit_length(total);
  const std::uint32_t rest = (1u << max_bits) - total;
  if (max_bits > huffman_code_length_max || (rest & (rest - 1)) != 0)
    return error::bad_weights;

  parsed.weights[stored] = static_cast<std::uint8_t>(bit_length(rest));
  parsed.symbol_count = stored + 1;
  return error::none;
}

// two interleaved states share one table; decoding ends when an update
// would read past the stream start (4.2.1.2)
result<std::size_t> decode_fse_weights(const std::byte *data, std::size_t size,
                                       huffman_weights &parsed) {
  auto dist = parse_fse_distribution(data, size, huffman_code_length_max + 1,
                                     huffman_weights_accuracy_log_max);
  if (!dist)
    return dist.err();

  fse_table table;
  const error err = build_fse_table(dist->counts.data(), dist->symbol_count,
                                    dist->accuracy_log, table);
  if (err != error::none)
    return err;

  auto stream =
      bit_reader::from_end(data + dist->consumed, size - dist->consumed);
  if (!stream)
    return stream.err();
  bit_reader bits = *stream;

  std::uint32_t state = bits.read(table.accuracy_log);
  std::uint32_t other = bits.read(table.accuracy_log);
  if (bits.overflowed())
    return error::corrupt_bitstream;

  std::size_t stored = 0;
  for (;;) {
    // decompressed size cannot exceed 255 weights, 4.2.1.2
    if (stored >= 255)
      return error::bad_weights;
    const fse_entry &entry = table.entries[state];
    parsed.weights[stored++] = entry.symbol;
    if (entry.num_bits > bits.bits_left()) {
      if (stored >= 255)
        return error::bad_weights;
      parsed.weights[stored++] = table.entries[other].symbol;
      break;
    }
    state = entry.baseline + bits.read(entry.num_bits);
    std::swap(state, other);
  }
  return stored;
}

} // namespace

result<huffman_weights> parse_huffman_weights(const std::byte *data,
                                              std::size_t size) {
  if (size == 0)
    return error::truncated_input;
  const unsigned header = std::to_integer<unsigned>(data[0]);
  huffman_weights parsed;
  std::size_t stored = 0;

  if (header < 128) {
    // headerByte is the size of the fse-compressed weights, 4.2.1.1
    if (size < std::size_t{1} + header)
      return error::truncated_input;
    auto decoded = decode_fse_weights(data + 1, header, parsed);
    if (!decoded)
      return decoded.err();
    stored = *decoded;
    parsed.consumed = std::size_t{1} + header;
  } else {
    // direct form, one 4-bit weight per stored symbol, 4.2.1.1
    stored = header - 127;
    const std::size_t payload = (stored + 1) / 2;
    if (size < 1 + payload)
      return error::truncated_input;
    for (std::size_t i = 0; i < stored; ++i) {
      const unsigned pair = std::to_integer<unsigned>(data[1 + i / 2]);
      parsed.weights[i] =
          static_cast<std::uint8_t>(i % 2 == 0 ? pair >> 4 : pair & 0xF);
    }
    parsed.consumed = 1 + payload;
  }

  const error err = derive_last_weight(parsed, stored);
  if (err != error::none)
    return err;
  return parsed;
}

error build_huffman_table(const std::uint8_t *weights, std::size_t symbol_count,
                          huffman_table &table) {
  if (symbol_count < 2 || symbol_count > 256)
    return error::bad_weights;

  std::uint32_t total = 0;
  std::size_t nonzero = 0;
  for (std::size_t s = 0; s < symbol_count; ++s) {
    if (weights[s] > huffman_code_length_max)
      return error::bad_weights;
    if (weights[s] != 0) {
      ++nonzero;
      total += 1u << (weights[s] - 1);
    }
  }
  if (nonzero < 2 || (total & (total - 1)) != 0)
    return error::bad_weights;
  const unsigned max_bits = bit_length(total) - 1;
  if (max_bits > huffman_code_length_max)
    return error::bad_weights;

  // by weight, then natural symbol order; a code of weight w spans
  // 2^(w-1) consecutive cells of the flat table (4.2.1.3)
  std::size_t position = 0;
  for (unsigned w = 1; w <= max_bits; ++w)
    for (std::size_t s = 0; s < symbol_count; ++s) {
      if (weights[s] != w)
        continue;
      const std::size_t span = std::size_t{1} << (w - 1);
      for (std::size_t i = 0; i < span; ++i) {
        table.entries[position + i].symbol = static_cast<std::uint8_t>(s);
        table.entries[position + i].num_bits =
            static_cast<std::uint8_t>(max_bits + 1 - w);
      }
      position += span;
    }

  table.max_num_bits = static_cast<std::uint8_t>(max_bits);
  return error::none;
}

} // namespace crunch
