#include <crunch/bitstream.hpp>
#include <crunch/fse.hpp>
#include <crunch/sequences.hpp>

#include <cstring>

namespace crunch {

namespace {

// builds one table according to its mode, consuming any description
// bytes; repeat mode keeps whatever the table already holds
error setup_table(compression_mode mode, const std::byte *&payload,
                  std::size_t &remaining, fse_table &table, bool have_previous,
                  std::size_t max_symbols, unsigned accuracy_log_max,
                  const std::int16_t *default_counts, std::size_t default_count,
                  unsigned default_log) {
  switch (mode) {
  case compression_mode::predefined:
    return build_fse_table(default_counts, default_count, default_log, table);
  case compression_mode::rle: {
    if (remaining < 1)
      return error::truncated_input;
    const unsigned symbol = std::to_integer<unsigned>(payload[0]);
    if (symbol >= max_symbols)
      return error::bad_distribution;
    table.entries[0] = fse_entry{0, 0, static_cast<std::uint8_t>(symbol)};
    table.accuracy_log = 0;
    payload += 1;
    remaining -= 1;
    return error::none;
  }
  case compression_mode::fse: {
    auto dist = parse_fse_distribution(payload, remaining, max_symbols,
                                       accuracy_log_max);
    if (!dist)
      return dist.err();
    const error err = build_fse_table(dist->counts.data(), dist->symbol_count,
                                      dist->accuracy_log, table);
    if (err != error::none)
      return err;
    payload += dist->consumed;
    remaining -= dist->consumed;
    return error::none;
  }
  case compression_mode::repeat:
    return have_previous ? error::none : error::missing_table;
  }
  return error::bad_distribution;
}

} // namespace

result<sequences_section_header>
parse_sequences_section_header(const std::byte *data, std::size_t size) {
  if (size == 0)
    return error::truncated_input;
  const unsigned byte0 = std::to_integer<unsigned>(data[0]);
  sequences_section_header header;

  // 0 means no sequences and no modes byte, 3.1.1.3.2.1
  if (byte0 == 0) {
    header.header_size = 1;
    return header;
  }

  std::size_t used = 1;
  if (byte0 < 128) {
    header.sequence_count = byte0;
  } else if (byte0 < 255) {
    if (size < 2)
      return error::truncated_input;
    header.sequence_count =
        ((byte0 - 128) << 8) + std::to_integer<unsigned>(data[1]);
    used = 2;
  } else {
    if (size < 3)
      return error::truncated_input;
    header.sequence_count = std::to_integer<unsigned>(data[1]) +
                            (std::to_integer<unsigned>(data[2]) << 8) + 0x7F00;
    used = 3;
  }

  if (size < used + 1)
    return error::truncated_input;
  const unsigned modes = std::to_integer<unsigned>(data[used]);
  if ((modes & 0x3) != 0)
    return error::reserved_bit_set;
  header.literals_lengths_mode = static_cast<compression_mode>(modes >> 6);
  header.offsets_mode = static_cast<compression_mode>((modes >> 4) & 0x3);
  header.match_lengths_mode = static_cast<compression_mode>((modes >> 2) & 0x3);
  header.header_size = static_cast<std::uint8_t>(used + 1);
  return header;
}

error decode_sequences(const sequences_section_header &header,
                       const std::byte *data, std::size_t size,
                       sequence_tables &tables, sequence *out) {
  if (size < header.header_size)
    return error::truncated_input;
  if (header.sequence_count == 0)
    return size == header.header_size ? error::none : error::corrupt_bitstream;

  const std::byte *payload = data + header.header_size;
  std::size_t remaining = size - header.header_size;

  // table descriptions come in literals length, offset, match length order
  error err =
      setup_table(header.literals_lengths_mode, payload, remaining,
                  tables.literals_lengths, tables.valid,
                  literals_length_code_count, literals_length_accuracy_log_max,
                  literals_length_default_distribution.data(),
                  literals_length_default_distribution.size(),
                  literals_length_default_accuracy_log);
  if (err != error::none)
    return err;
  err = setup_table(header.offsets_mode, payload, remaining, tables.offsets,
                    tables.valid, offset_code_supported_max + 1,
                    offset_accuracy_log_max, offset_default_distribution.data(),
                    offset_default_distribution.size(),
                    offset_default_accuracy_log);
  if (err != error::none)
    return err;
  err = setup_table(header.match_lengths_mode, payload, remaining,
                    tables.match_lengths, tables.valid, match_length_code_count,
                    match_length_accuracy_log_max,
                    match_length_default_distribution.data(),
                    match_length_default_distribution.size(),
                    match_length_default_accuracy_log);
  if (err != error::none)
    return err;
  tables.valid = true;

  auto stream = bit_reader::from_end(payload, remaining);
  if (!stream)
    return error::corrupt_bitstream;
  bit_reader bits = *stream;

  // states initialize in literals, offset, match order (3.1.1.3.2.1.2)
  std::uint32_t ll_state = bits.read(tables.literals_lengths.accuracy_log);
  std::uint32_t of_state = bits.read(tables.offsets.accuracy_log);
  std::uint32_t ml_state = bits.read(tables.match_lengths.accuracy_log);
  if (bits.overflowed())
    return error::corrupt_bitstream;

  for (std::uint32_t i = 0; i < header.sequence_count; ++i) {
    const fse_entry &ll = tables.literals_lengths.entries[ll_state];
    const fse_entry &of = tables.offsets.entries[of_state];
    const fse_entry &ml = tables.match_lengths.entries[ml_state];

    // raw bits come offset first, then match length, then literals length
    out[i].offset = (1u << of.symbol) + bits.read(of.symbol);
    const sequence_code &ml_code = match_length_codes[ml.symbol];
    out[i].match_length = ml_code.baseline + bits.read(ml_code.num_bits);
    const sequence_code &ll_code = literals_length_codes[ll.symbol];
    out[i].literals_length = ll_code.baseline + bits.read(ll_code.num_bits);

    // updates run in literals, match, offset order and skip the last
    // sequence
    if (i + 1 < header.sequence_count) {
      ll_state = ll.baseline + bits.read(ll.num_bits);
      ml_state = ml.baseline + bits.read(ml.num_bits);
      of_state = of.baseline + bits.read(of.num_bits);
    }
  }

  if (bits.overflowed() || bits.bits_left() != 0)
    return error::corrupt_bitstream;
  return error::none;
}

error execute_sequences(const sequence *sequences, std::size_t count,
                        const std::byte *literals, std::size_t literals_size,
                        const std::byte *history, std::size_t history_size,
                        repeat_offsets &recent, std::byte *dst,
                        std::size_t dst_capacity, std::size_t &written) {
  std::size_t literal_pos = 0;

  for (std::size_t i = 0; i < count; ++i) {
    const sequence &seq = sequences[i];

    // offset values 1-3 select recent offsets, shifted by one when the
    // sequence has no literals (3.1.1.5)
    std::uint32_t offset;
    if (seq.offset > 3 || (seq.offset == 3 && seq.literals_length == 0)) {
      offset = seq.offset > 3 ? seq.offset - 3 : recent.value[0] - 1;
      if (offset == 0)
        return error::corrupt_bitstream;
      recent.value[2] = recent.value[1];
      recent.value[1] = recent.value[0];
      recent.value[0] = offset;
    } else {
      const unsigned pick =
          seq.literals_length == 0 ? seq.offset : seq.offset - 1;
      offset = recent.value[pick];
      for (unsigned j = pick; j > 0; --j)
        recent.value[j] = recent.value[j - 1];
      recent.value[0] = offset;
    }

    if (seq.literals_length > literals_size - literal_pos)
      return error::corrupt_bitstream;
    if (seq.literals_length > dst_capacity - written)
      return error::output_too_small;
    std::memcpy(dst + written, literals + literal_pos, seq.literals_length);
    literal_pos += seq.literals_length;
    written += seq.literals_length;

    if (offset > written + history_size)
      return error::corrupt_bitstream;
    if (seq.match_length > dst_capacity - written)
      return error::output_too_small;
    // overlapping matches repeat the pattern, so copy byte by byte; a
    // match may start in the history and run into the frame's output
    for (std::uint32_t j = 0; j < seq.match_length; ++j, ++written)
      dst[written] = offset > written ? history[history_size - offset + written]
                                      : dst[written - offset];
  }

  const std::size_t leftover = literals_size - literal_pos;
  if (leftover > dst_capacity - written)
    return error::output_too_small;
  std::memcpy(dst + written, literals + literal_pos, leftover);
  written += leftover;
  return error::none;
}

} // namespace crunch
