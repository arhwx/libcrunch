#include <crunch/bitstream.hpp>
#include <crunch/dictionary.hpp>
#include <crunch/fse.hpp>
#include <crunch/huffman.hpp>

#include <optional>

namespace crunch {

namespace {

// parses one distribution and builds its table into slot
error load_fse_table(const std::byte *&payload, std::size_t &remaining,
                     std::optional<fse_table> &slot, std::size_t max_symbols,
                     unsigned accuracy_log_max) {
  auto dist =
      parse_fse_distribution(payload, remaining, max_symbols, accuracy_log_max);
  if (!dist)
    return dist.err();
  slot.emplace();
  const error err = build_fse_table(dist->counts.data(), dist->symbol_count,
                                    dist->accuracy_log, *slot);
  if (err != error::none)
    return err;
  payload += dist->consumed;
  remaining -= dist->consumed;
  return error::none;
}

} // namespace

error parse_dictionary(const std::byte *data, std::size_t size,
                       dictionary &dict) {
  // raw content dictionaries have no header but still need 8 bytes, 5
  if (size < 8)
    return error::bad_dictionary;

  dict = dictionary{};
  if (read_le32(data) != dictionary_magic) {
    dict.content = data;
    dict.content_size = size;
    return error::none;
  }

  dict.id = read_le32(data + 4);
  const std::byte *payload = data + 8;
  std::size_t remaining = size - 8;

  auto weights = parse_huffman_weights(payload, remaining);
  if (!weights)
    return weights.err();
  dict.literals_huffman.emplace();
  const error huffman_err = build_huffman_table(
      weights->weights.data(), weights->symbol_count, *dict.literals_huffman);
  if (huffman_err != error::none)
    return huffman_err;
  payload += weights->consumed;
  remaining -= weights->consumed;

  // fse tables come in offset, match length, literals length order
  error err =
      load_fse_table(payload, remaining, dict.offset_table,
                     offset_code_supported_max + 1, offset_accuracy_log_max);
  if (err != error::none)
    return err;
  err = load_fse_table(payload, remaining, dict.match_length_table,
                       match_length_code_count, match_length_accuracy_log_max);
  if (err != error::none)
    return err;
  err = load_fse_table(payload, remaining, dict.literals_length_table,
                       literals_length_code_count,
                       literals_length_accuracy_log_max);
  if (err != error::none)
    return err;

  if (remaining < 12)
    return error::truncated_input;
  for (std::size_t i = 0; i < 3; ++i)
    dict.recent_offsets.value[i] = read_le32(payload + 4 * i);
  dict.content = payload + 12;
  dict.content_size = remaining - 12;

  for (const std::uint32_t offset : dict.recent_offsets.value)
    if (offset == 0 || offset > dict.content_size)
      return error::bad_dictionary;
  return error::none;
}

} // namespace crunch
