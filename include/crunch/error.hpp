#pragma once

#include <cstdint>
#include <utility>

namespace crunch {

enum class error : std::uint8_t {
  none = 0,
  truncated_input,
  reserved_bit_set,    // frame header reserved bit, 3.1.1.1.1.4
  reserved_block_type, // 3.1.1.2.2
  corrupt_bitstream,   // missing sentinel bit or read past stream start
};

template <typename T> class result {
public:
  result(T value) : value_(std::move(value)) {}
  result(error err) : err_(err) {}

  explicit operator bool() const { return err_ == error::none; }
  error err() const { return err_; }
  T &value() { return value_; }
  const T &value() const { return value_; }

private:
  T value_{};
  error err_ = error::none;
};

} // namespace crunch
