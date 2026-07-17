#pragma once

#include <cstdint>
#include <utility>

namespace crunch {

enum class error : std::uint8_t {
  none = 0,
  truncated_input,
  reserved_bit_set,      // frame header reserved bit, 3.1.1.1.1.4
  reserved_block_type,   // 3.1.1.2.2
  corrupt_bitstream,     // missing sentinel bit or read past stream start
  bad_magic,             // neither a zstd nor a skippable frame
  block_too_large,       // over Block_Maximum_Size, 3.1.1.2.4
  output_too_small,      // destination cannot hold the decoded content
  checksum_mismatch,     // 3.1.1
  content_size_mismatch, // output differs from Frame_Content_Size, 3.1.1.1.4
  bad_distribution,      // fse probabilities do not fit the table, 4.1.1
  bad_weights,           // huffman weights cannot form a prefix code, 4.2.1
  unsupported,           // valid input the decoder cannot handle yet
};

template <typename T> class result {
public:
  result(T value) : value_(std::move(value)) {}
  result(error err) : err_(err) {}

  explicit operator bool() const { return err_ == error::none; }
  error err() const { return err_; }
  T &operator*() { return value_; }
  const T &operator*() const { return value_; }
  T *operator->() { return &value_; }
  const T *operator->() const { return &value_; }

private:
  T value_{};
  error err_ = error::none;
};

} // namespace crunch
