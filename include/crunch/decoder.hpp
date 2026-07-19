#pragma once

#include <crunch/block.hpp>
#include <crunch/error.hpp>
#include <crunch/format.hpp>
#include <crunch/xxhash.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace crunch {

// frames requesting a larger window fail with window_too_large (3.1.1.1.2)
inline constexpr std::uint64_t default_window_limit = 128ull * 1024 * 1024;

enum class decode_status : std::uint8_t {
  frame_complete, // a frame ended and all of its output was delivered
  need_input,     // src is exhausted mid-frame
  output_full,    // dst is full, call again with more room
};

// incremental decoder over arbitrary input and output slices
class decoder {
public:
  std::uint64_t window_limit = default_window_limit; // read at frame start
  const dictionary *dict = nullptr;

  result<decode_status> decode(const std::byte *src, std::size_t src_size,
                               std::size_t &src_pos, std::byte *dst,
                               std::size_t dst_size, std::size_t &dst_pos);

  // drops any partial frame and waits for a new one
  void reset();

private:
  enum class phase : std::uint8_t {
    frame_start,
    skip_payload,
    block_start,
    block_payload,
    frame_checksum,
    frame_finish,
  };

  error start_frame();
  error process_block(const std::byte *payload);
  bool drain(std::byte *dst, std::size_t dst_size, std::size_t &dst_pos);
  bool stage(const std::byte *src, std::size_t src_size, std::size_t &src_pos,
             std::size_t need);

  phase phase_ = phase::frame_start;
  std::vector<std::byte> staging_; // partial header or block payload
  std::vector<std::byte> buffer_;  // frame output, slid to keep one window
  frame_header header_;
  block_header block_;
  block_context entropy_;
  xxh64_state hash_;
  std::uint64_t window_size_ = 0;
  std::uint64_t block_max_ = 0;
  std::uint64_t frame_written_ = 0;
  std::uint64_t skip_remaining_ = 0;
  std::size_t payload_need_ = 0;
  std::size_t pos_ = 0;  // buffer write position
  std::size_t emit_ = 0; // next undelivered buffer byte
};

} // namespace crunch
