#pragma once

#include <crunch/error.hpp>
#include <crunch/format.hpp>

#include <cstddef>

namespace crunch {

result<literals_section_header>
parse_literals_section_header(const std::byte *data, std::size_t size);

// data spans the whole section, header included; table and have_table
// carry the huffman tree across a frame's blocks (3.1.1.3.1)
error decode_literals(const literals_section_header &header,
                      const std::byte *data, std::size_t size,
                      huffman_table &table, bool &have_table, std::byte *dst,
                      std::size_t dst_size);

} // namespace crunch
