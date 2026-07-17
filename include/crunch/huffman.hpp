#pragma once

#include <crunch/error.hpp>
#include <crunch/format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace crunch {

// parsed form of a Huffman tree description (4.2.1), implicit last
// weight included
struct huffman_weights {
  std::array<std::uint8_t, 256> weights{};
  std::size_t symbol_count = 0;
  std::size_t consumed = 0; // bytes, header byte included
};

result<huffman_weights> parse_huffman_weights(const std::byte *data,
                                              std::size_t size);

// weights must be a complete set, summing to a power of 2 (4.2.1.3)
error build_huffman_table(const std::uint8_t *weights, std::size_t symbol_count,
                          huffman_table &table);

} // namespace crunch
