#include <crunch/bitstream.hpp>
#include <crunch/frame.hpp>

namespace crunch {

result<frame_header> parse_frame_header(const std::byte *data, std::size_t size,
                                        std::size_t &consumed) {
  if (size < 1)
    return error::truncated_input;
  const frame_header_descriptor desc{std::to_integer<std::uint8_t>(data[0])};
  if (desc.reserved_bit())
    return error::reserved_bit_set;

  const unsigned did_size = desc.did_field_size();
  const unsigned fcs_size = desc.fcs_field_size();
  const std::size_t total =
      1 + (desc.single_segment() ? 0 : 1) + did_size + fcs_size;
  if (size < total)
    return error::truncated_input;

  frame_header hdr;
  hdr.single_segment = desc.single_segment();
  hdr.has_checksum = desc.content_checksum();

  const std::byte *p = data + 1;
  if (!desc.single_segment()) {
    const window_descriptor wd{std::to_integer<std::uint8_t>(*p)};
    hdr.window_size = wd.window_size();
    ++p;
  }

  switch (did_size) {
  case 1:
    hdr.dictionary_id = std::to_integer<std::uint8_t>(*p);
    break;
  case 2:
    hdr.dictionary_id = read_le16(p);
    break;
  case 4:
    hdr.dictionary_id = read_le32(p);
    break;
  }
  p += did_size;

  switch (fcs_size) {
  case 1:
    hdr.content_size = std::to_integer<std::uint8_t>(*p);
    break;
  case 2:
    hdr.content_size = read_le16(p) + 256u;
    break;
  case 4:
    hdr.content_size = read_le32(p);
    break;
  case 8:
    hdr.content_size = read_le64(p);
    break;
  }

  // no window descriptor in this case; the content is one segment
  if (desc.single_segment())
    hdr.window_size = *hdr.content_size;

  consumed = total;
  return hdr;
}

result<block_header> parse_block_header(const std::byte *data,
                                        std::size_t size) {
  if (size < block_header_size)
    return error::truncated_input;
  const std::uint32_t raw = read_le24(data);
  block_header hdr;
  hdr.last_block = (raw & 1) != 0;
  hdr.type = static_cast<block_type>(raw >> 1 & 3);
  hdr.block_size = raw >> 3;
  if (hdr.type == block_type::reserved)
    return error::reserved_block_type;
  return hdr;
}

} // namespace crunch
