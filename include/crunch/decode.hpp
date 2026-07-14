#pragma once

#include <crunch/error.hpp>

#include <cstddef>

namespace crunch {

// decodes one frame into dst and returns the bytes written, none for
// skippable frames; consumed is set on success
result<std::size_t> decode_frame(const std::byte *src, std::size_t src_size,
                                 std::byte *dst, std::size_t dst_capacity,
                                 std::size_t &consumed);

} // namespace crunch
