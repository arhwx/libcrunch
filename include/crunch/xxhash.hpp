#pragma once

#include <crunch/bitstream.hpp>

#include <cstddef>
#include <cstdint>

namespace crunch {

// XXH64, https://github.com/Cyan4973/xxHash
// The frame content checksum is the low 4 bytes of this hash over the
// decoded content, seeded with zero (3.1.1).

inline constexpr std::uint64_t xxh_prime_1 = 0x9E3779B185EBCA87;
inline constexpr std::uint64_t xxh_prime_2 = 0xC2B2AE3D27D4EB4F;
inline constexpr std::uint64_t xxh_prime_3 = 0x165667B19E3779F9;
inline constexpr std::uint64_t xxh_prime_4 = 0x85EBCA77C2B2AE63;
inline constexpr std::uint64_t xxh_prime_5 = 0x27D4EB2F165667C5;

inline std::uint64_t rotl64(std::uint64_t v, unsigned bits) {
  return v << bits | v >> (64 - bits);
}

inline std::uint64_t xxh_round(std::uint64_t acc, std::uint64_t input) {
  return rotl64(acc + input * xxh_prime_2, 31) * xxh_prime_1;
}

inline std::uint64_t xxh_merge(std::uint64_t hash, std::uint64_t acc) {
  return (hash ^ xxh_round(0, acc)) * xxh_prime_1 + xxh_prime_4;
}

inline std::uint64_t xxh64(const std::byte *data, std::size_t size,
                           std::uint64_t seed = 0) {
  std::size_t i = 0;
  std::uint64_t hash;

  if (size >= 32) {
    std::uint64_t v1 = seed + xxh_prime_1 + xxh_prime_2;
    std::uint64_t v2 = seed + xxh_prime_2;
    std::uint64_t v3 = seed;
    std::uint64_t v4 = seed - xxh_prime_1;
    for (; size - i >= 32; i += 32) {
      v1 = xxh_round(v1, read_le64(data + i));
      v2 = xxh_round(v2, read_le64(data + i + 8));
      v3 = xxh_round(v3, read_le64(data + i + 16));
      v4 = xxh_round(v4, read_le64(data + i + 24));
    }
    hash = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
    hash = xxh_merge(hash, v1);
    hash = xxh_merge(hash, v2);
    hash = xxh_merge(hash, v3);
    hash = xxh_merge(hash, v4);
  } else {
    hash = seed + xxh_prime_5;
  }

  hash += size;

  for (; size - i >= 8; i += 8) {
    hash ^= xxh_round(0, read_le64(data + i));
    hash = rotl64(hash, 27) * xxh_prime_1 + xxh_prime_4;
  }
  if (size - i >= 4) {
    hash ^= read_le32(data + i) * xxh_prime_1;
    hash = rotl64(hash, 23) * xxh_prime_2 + xxh_prime_3;
    i += 4;
  }
  for (; i < size; ++i) {
    hash ^= std::to_integer<std::uint64_t>(data[i]) * xxh_prime_5;
    hash = rotl64(hash, 11) * xxh_prime_1;
  }

  hash ^= hash >> 33;
  hash *= xxh_prime_2;
  hash ^= hash >> 29;
  hash *= xxh_prime_3;
  hash ^= hash >> 32;
  return hash;
}

} // namespace crunch
