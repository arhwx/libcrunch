#pragma once

#include <crunch/error.hpp>
#include <crunch/format.hpp>

#include <cstddef>

namespace crunch {

constexpr bool is_skippable_magic(std::uint32_t magic) {
  return (magic & 0xFFFFFFF0) == skippable_magic_min;
}

// data points just past the 4-byte magic; consumed is set on success
result<frame_header> parse_frame_header(const std::byte *data, std::size_t size,
                                        std::size_t &consumed);

result<block_header> parse_block_header(const std::byte *data,
                                        std::size_t size);

} // namespace crunch
