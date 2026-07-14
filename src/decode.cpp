#include <crunch/bitstream.hpp>
#include <crunch/decode.hpp>
#include <crunch/frame.hpp>
#include <crunch/xxhash.hpp>

#include <cstring>

namespace crunch {

result<std::size_t> decode_frame(const std::byte *src, std::size_t src_size,
                                 std::byte *dst, std::size_t dst_capacity,
                                 std::size_t &consumed) {
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
  const frame_header &hdr = parsed.value();
  off += header_size;

  const std::uint64_t block_max =
      hdr.window_size < block_size_cap ? hdr.window_size : block_size_cap;
  std::size_t written = 0;

  for (bool last = false; !last;) {
    const auto blk = parse_block_header(src + off, src_size - off);
    if (!blk)
      return blk.err();
    off += block_header_size;
    last = blk.value().last_block;
    const std::size_t block_size = blk.value().block_size;
    if (block_size > block_max)
      return error::block_too_large;

    switch (blk.value().type) {
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
    case block_type::compressed:
      // needs entropy decoding, not built yet
      return error::unsupported;
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
