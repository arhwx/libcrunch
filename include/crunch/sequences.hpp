#pragma once

#include <crunch/error.hpp>
#include <crunch/format.hpp>

#include <cstddef>

namespace crunch {

// the three fse tables persist across a frame's blocks for repeat mode
// (3.1.1.3.2.1); an rle table is one entry with num_bits 0
struct sequence_tables {
  fse_table literals_lengths;
  fse_table offsets;
  fse_table match_lengths;
  bool valid = false;
};

result<sequences_section_header>
parse_sequences_section_header(const std::byte *data, std::size_t size);

// data spans the whole section, header included; out receives raw
// offset values, translated during execution (3.1.1.4)
error decode_sequences(const sequences_section_header &header,
                       const std::byte *data, std::size_t size,
                       sequence_tables &tables, sequence *out);

// dst is the whole frame output so far, so matches can reach previous
// blocks (3.1.1.4, 3.1.1.5)
error execute_sequences(const sequence *sequences, std::size_t count,
                        const std::byte *literals, std::size_t literals_size,
                        repeat_offsets &recent, std::byte *dst,
                        std::size_t dst_capacity, std::size_t &written);

} // namespace crunch
