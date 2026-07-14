#include <crunch/bitstream.hpp>
#include <crunch/fse.hpp>

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

} // namespace

result<fse_distribution> parse_fse_distribution(const std::byte *data,
                                                std::size_t size,
                                                std::size_t max_symbols,
                                                unsigned accuracy_log_max) {
  if (size == 0)
    return error::truncated_input;
  forward_bit_reader stream(data, size);

  fse_distribution dist;
  dist.accuracy_log = stream.read(4) + 5;
  if (dist.accuracy_log > accuracy_log_max)
    return error::bad_distribution;

  // remaining points + 1; the field width shrinks as points run out
  std::int32_t remaining = (1 << dist.accuracy_log) + 1;
  std::size_t symbol = 0;
  std::size_t nonzero = 0;

  while (remaining > 1) {
    if (stream.overflowed())
      return error::truncated_input;
    if (symbol >= max_symbols)
      return error::bad_distribution;

    const unsigned width = bit_length(static_cast<std::uint32_t>(remaining));
    const std::uint32_t low_threshold =
        (1u << width) - 1 - static_cast<std::uint32_t>(remaining);
    const std::uint32_t small_mask = (1u << (width - 1)) - 1;

    // small values save one bit, Table 20
    std::uint32_t value = stream.peek(width);
    if ((value & small_mask) < low_threshold) {
      value &= small_mask;
      stream.advance(width - 1);
    } else {
      value &= (1u << width) - 1;
      if (value >= 1u << (width - 1))
        value -= low_threshold;
      stream.advance(width);
    }

    const std::int32_t probability = static_cast<std::int32_t>(value) - 1;
    remaining -= probability < 0 ? 1 : probability;
    if (probability != 0)
      ++nonzero;
    dist.counts[symbol++] = static_cast<std::int16_t>(probability);

    if (probability == 0) {
      for (;;) {
        const std::uint32_t repeat = stream.read(2);
        for (std::uint32_t i = 0; i < repeat; ++i) {
          if (symbol >= max_symbols)
            return error::bad_distribution;
          dist.counts[symbol++] = 0;
        }
        if (repeat != 3)
          break;
      }
    }
  }

  if (stream.overflowed())
    return error::truncated_input;
  if (nonzero < 2)
    return error::bad_distribution;

  dist.symbol_count = symbol;
  dist.consumed = (stream.bits_read() + 7) / 8;
  return dist;
}

error build_fse_table(const std::int16_t *counts, std::size_t symbol_count,
                      unsigned accuracy_log, fse_table &table) {
  if (accuracy_log == 0 || accuracy_log > fse_accuracy_log_max ||
      symbol_count > 256)
    return error::bad_distribution;
  const std::size_t table_size = std::size_t{1} << accuracy_log;

  std::int32_t total = 0;
  std::size_t nonzero = 0;
  for (std::size_t s = 0; s < symbol_count; ++s) {
    if (counts[s] < -1)
      return error::bad_distribution;
    if (counts[s] != 0)
      ++nonzero;
    total += counts[s] == -1 ? 1 : counts[s];
  }
  if (total != static_cast<std::int32_t>(table_size) || nonzero < 2)
    return error::bad_distribution;

  // low-probability symbols take the top cells, in natural order
  std::size_t high = table_size - 1;
  for (std::size_t s = 0; s < symbol_count; ++s)
    if (counts[s] == -1)
      table.entries[high--].symbol = static_cast<std::uint8_t>(s);

  const std::size_t step = (table_size >> 1) + (table_size >> 3) + 3;
  const std::size_t mask = table_size - 1;
  std::size_t position = 0;
  for (std::size_t s = 0; s < symbol_count; ++s)
    for (std::int32_t i = 0; i < counts[s]; ++i) {
      table.entries[position].symbol = static_cast<std::uint8_t>(s);
      do
        position = (position + step) & mask;
      while (position > high);
    }

  // states in natural order take n = count, count + 1, ... per symbol;
  // a low-probability cell behaves as count 1, a full state reset
  std::array<std::uint16_t, 256> next{};
  for (std::size_t s = 0; s < symbol_count; ++s)
    next[s] = counts[s] == -1 ? 1 : static_cast<std::uint16_t>(counts[s]);

  for (std::size_t state = 0; state < table_size; ++state) {
    fse_entry &entry = table.entries[state];
    const std::uint16_t n = next[entry.symbol]++;
    entry.num_bits =
        static_cast<std::uint8_t>(accuracy_log - bit_length(n) + 1);
    entry.baseline = static_cast<std::uint16_t>(
        (static_cast<std::size_t>(n) << entry.num_bits) - table_size);
  }
  table.accuracy_log = static_cast<std::uint8_t>(accuracy_log);
  return error::none;
}

} // namespace crunch
