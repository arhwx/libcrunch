#include <crunch/bitstream.hpp>
#include <crunch/block.hpp>
#include <crunch/decode.hpp>
#include <crunch/frame.hpp>
#include <crunch/literals.hpp>
#include <crunch/sequences.hpp>
#include <crunch/xxhash.hpp>

#include <cstring>
#include <vector>

namespace crunch {

void seed_block_context(block_context &context, const dictionary &dict) {
  context.history = dict.content;
  context.history_size = dict.content_size;
  context.recent = dict.recent_offsets;
  if (dict.literals_huffman) {
    context.literals_table = *dict.literals_huffman;
    context.have_literals_table = true;
  }
  if (dict.offset_table && dict.match_length_table &&
      dict.literals_length_table) {
    context.tables.offsets = *dict.offset_table;
    context.tables.match_lengths = *dict.match_length_table;
    context.tables.literals_lengths = *dict.literals_length_table;
    context.tables.valid = true;
  }
}

error decode_compressed_block(const std::byte *src, std::size_t size,
                              block_context &state, std::byte *dst,
                              std::size_t dst_capacity, std::size_t &written,
                              std::uint64_t block_max) {
  auto lit_header = parse_literals_section_header(src, size);
  if (!lit_header)
    return lit_header.err();
  if (lit_header->regenerated_size > block_max)
    return error::block_too_large;
  if (state.literals.empty())
    state.literals.resize(block_size_cap);

  const error lit_err = decode_literals(
      *lit_header, src, size, state.literals_table, state.have_literals_table,
      state.literals.data(), state.literals.size());
  if (lit_err != error::none)
    return lit_err;

  std::size_t literal_bytes = 0;
  switch (lit_header->type) {
  case literals_block_type::raw:
    literal_bytes = lit_header->regenerated_size;
    break;
  case literals_block_type::rle:
    literal_bytes = 1;
    break;
  default:
    literal_bytes = lit_header->compressed_size;
    break;
  }
  const std::size_t section = lit_header->header_size + literal_bytes;

  auto seq_header =
      parse_sequences_section_header(src + section, size - section);
  if (!seq_header)
    return seq_header.err();
  std::vector<sequence> sequences(seq_header->sequence_count);
  const error seq_err =
      decode_sequences(*seq_header, src + section, size - section, state.tables,
                       sequences.data());
  if (seq_err != error::none)
    return seq_err;

  const std::size_t before = written;
  const error exec_err = execute_sequences(
      sequences.data(), sequences.size(), state.literals.data(),
      lit_header->regenerated_size, state.history, state.history_size,
      state.window, state.recent, dst, dst_capacity, written);
  if (exec_err != error::none)
    return exec_err;
  // Block_Maximum_Size caps the regenerated size too, 3.1.1.2.4
  if (written - before > block_max)
    return error::block_too_large;
  return error::none;
}

result<std::size_t> decode_frame(const std::byte *src, std::size_t src_size,
                                 std::byte *dst, std::size_t dst_capacity,
                                 std::size_t &consumed) {
  return decode_frame(src, src_size, dst, dst_capacity, consumed, nullptr);
}

result<std::size_t> decode_frame(const std::byte *src, std::size_t src_size,
                                 std::byte *dst, std::size_t dst_capacity,
                                 std::size_t &consumed,
                                 const dictionary *dict) {
  if (src_size < 4)
    return error::truncated_input;
  const std::uint32_t magic = read_le32(src);

  if (is_skippable_magic(magic)) {
    if (src_size < 8)
      return error::truncated_input;
    const std::size_t total = 8 + read_le32(src + 4);
    if (src_size < total)
      return error::truncated_input;
    consumed = total;
    return std::size_t{0};
  }

  if (magic != frame_magic)
    return error::bad_magic;

  std::size_t off = 4;
  std::size_t header_size = 0;
  const auto parsed =
      parse_frame_header(src + off, src_size - off, header_size);
  if (!parsed)
    return parsed.err();
  const frame_header &hdr = *parsed;
  off += header_size;

  if (hdr.dictionary_id != 0 &&
      (dict == nullptr || dict->id != hdr.dictionary_id))
    return error::wrong_dictionary;

  const std::uint64_t block_max =
      hdr.window_size < block_size_cap ? hdr.window_size : block_size_cap;
  std::size_t written = 0;
  block_context state;
  state.window = hdr.window_size;
  if (dict != nullptr)
    seed_block_context(state, *dict);

  for (bool last = false; !last;) {
    const auto blk = parse_block_header(src + off, src_size - off);
    if (!blk)
      return blk.err();
    off += block_header_size;
    last = blk->last_block;
    const std::size_t block_size = blk->block_size;
    if (block_size > block_max)
      return error::block_too_large;

    switch (blk->type) {
    case block_type::raw:
      if (src_size - off < block_size)
        return error::truncated_input;
      if (dst_capacity - written < block_size)
        return error::output_too_small;
      std::memcpy(dst + written, src + off, block_size);
      off += block_size;
      written += block_size;
      break;
    case block_type::rle:
      if (src_size - off < 1)
        return error::truncated_input;
      if (dst_capacity - written < block_size)
        return error::output_too_small;
      std::memset(dst + written, std::to_integer<int>(src[off]), block_size);
      off += 1;
      written += block_size;
      break;
    case block_type::compressed: {
      if (src_size - off < block_size)
        return error::truncated_input;
      const error err = decode_compressed_block(
          src + off, block_size, state, dst, dst_capacity, written, block_max);
      if (err != error::none)
        return err;
      off += block_size;
      break;
    }
    case block_type::reserved:
      return error::reserved_block_type;
    }
  }

  if (hdr.content_size && written != *hdr.content_size)
    return error::content_size_mismatch;

  if (hdr.has_checksum) {
    if (src_size - off < 4)
      return error::truncated_input;
    if (static_cast<std::uint32_t>(xxh64(dst, written)) != read_le32(src + off))
      return error::checksum_mismatch;
    off += 4;
  }

  consumed = off;
  return written;
}

} // namespace crunch
