#pragma once

#include <crunch/error.hpp>
#include <crunch/format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace crunch {

// parsed form of an FSE table description (4.1.1)
struct fse_distribution {
  std::array<std::int16_t, 256> counts{};
  std::size_t symbol_count = 0;
  unsigned accuracy_log = 0;
  std::size_t consumed = 0; // bytes, always a round number
};

// max_symbols and accuracy_log_max depend on where the table appears
result<fse_distribution> parse_fse_distribution(const std::byte *data,
                                                std::size_t size,
                                                std::size_t max_symbols,
                                                unsigned accuracy_log_max);

// counts of -1 take one state each at the table end (4.1.1)
error build_fse_table(const std::int16_t *counts, std::size_t symbol_count,
                      unsigned accuracy_log, fse_table &table);

} // namespace crunch
