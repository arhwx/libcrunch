#pragma once

#include <crunch/error.hpp>

#include <cstddef>
#include <cstdint>

namespace crunch {

inline std::uint16_t read_le16(const std::byte *p) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(p[0]) |
                                    std::to_integer<unsigned>(p[1]) << 8);
}

inline std::uint32_t read_le24(const std::byte *p) {
  return read_le16(p) | std::to_integer<std::uint32_t>(p[2]) << 16;
}

inline std::uint32_t read_le32(const std::byte *p) {
  return read_le24(p) | std::to_integer<std::uint32_t>(p[3]) << 24;
}

inline std::uint64_t read_le64(const std::byte *p) {
  return read_le32(p) | std::uint64_t{read_le32(p + 4)} << 32;
}

// Entropy bitstreams are read backward from their last byte (4.1). The
// highest set bit of the last byte is a sentinel marking the end of the
// stream, not data.
class bit_reader {
public:
  bit_reader() = default;

  static result<bit_reader> from_end(const std::byte *data, std::size_t size);

  // count <= 32; reading past the start of the stream flags overflow and
  // returns 0, so callers can check once after a decode loop
  std::uint32_t read(unsigned count);

  std::size_t bits_left() const { return bits_left_; }
  bool overflowed() const { return overflowed_; }

private:
  const std::byte *data_ = nullptr;
  std::size_t bits_left_ = 0;
  bool overflowed_ = false;
};

inline result<bit_reader> bit_reader::from_end(const std::byte *data,
                                               std::size_t size) {
  if (size == 0)
    return error::truncated_input;
  const unsigned last = std::to_integer<unsigned>(data[size - 1]);
  if (last == 0)
    return error::corrupt_bitstream;
  unsigned sentinel = 7;
  while ((last >> sentinel & 1) == 0)
    --sentinel;
  bit_reader r;
  r.data_ = data;
  r.bits_left_ = (size - 1) * 8 + sentinel;
  return r;
}

inline std::uint32_t bit_reader::read(unsigned count) {
  if (count > bits_left_) {
    overflowed_ = true;
    bits_left_ = 0;
    return 0;
  }
  bits_left_ -= count;
  const std::byte *p = data_ + bits_left_ / 8;
  const unsigned shift = bits_left_ % 8;
  std::uint64_t acc = 0;
  for (unsigned got = 0; got < shift + count; got += 8)
    acc |= std::to_integer<std::uint64_t>(p[got / 8]) << got;
  return static_cast<std::uint32_t>(acc >> shift) &
         static_cast<std::uint32_t>((std::uint64_t{1} << count) - 1);
}

} // namespace crunch
