#include <crunch/bitstream.hpp>
#include <crunch/decoder.hpp>
#include <crunch/frame.hpp>

#include <cstring>

namespace crunch {

bool decoder::stage(const std::byte *src, std::size_t src_size,
                    std::size_t &src_pos, std::size_t need) {
  if (staging_.size() < need) {
    const std::size_t want = need - staging_.size();
    const std::size_t avail = src_size - src_pos;
    const std::size_t take = want < avail ? want : avail;
    if (take > 0) {
      staging_.insert(staging_.end(), src + src_pos, src + src_pos + take);
      src_pos += take;
    }
  }
  return staging_.size() >= need;
}

bool decoder::drain(std::byte *dst, std::size_t dst_size,
                    std::size_t &dst_pos) {
  const std::size_t pending = pos_ - emit_;
  if (pending == 0)
    return true;
  const std::size_t room = dst_size - dst_pos;
  const std::size_t take = pending < room ? pending : room;
  if (take > 0) {
    std::memcpy(dst + dst_pos, buffer_.data() + emit_, take);
    dst_pos += take;
    emit_ += take;
  }
  return emit_ == pos_;
}

error decoder::start_frame() {
  if (header_.window_size > window_limit)
    return error::window_too_large;
  if (header_.dictionary_id != 0 &&
      (dict == nullptr || dict->id != header_.dictionary_id))
    return error::wrong_dictionary;

  window_size_ = header_.window_size;
  block_max_ = window_size_ < block_size_cap ? window_size_ : block_size_cap;
  // a window of history plus a window and a block of output between slides
  buffer_.resize(static_cast<std::size_t>(2 * window_size_ + block_max_));
  pos_ = 0;
  emit_ = 0;
  frame_written_ = 0;
  hash_.reset();

  entropy_.have_literals_table = false;
  entropy_.tables.valid = false;
  entropy_.recent = repeat_offsets{};
  entropy_.history = nullptr;
  entropy_.history_size = 0;
  entropy_.window = window_size_;
  if (dict != nullptr)
    seed_block_context(entropy_, *dict);

  phase_ = phase::block_start;
  return error::none;
}

error decoder::process_block(const std::byte *payload) {
  if (frame_written_ > window_size_) {
    entropy_.history = nullptr;
    entropy_.history_size = 0;
  }

  // when a full block doesn't fit, slide the delivered part down
  if (buffer_.size() - pos_ < block_max_ && pos_ > window_size_) {
    const std::size_t keep = static_cast<std::size_t>(window_size_);
    std::memmove(buffer_.data(), buffer_.data() + (pos_ - keep), keep);
    pos_ = keep;
    emit_ = keep;
  }

  const std::size_t before = pos_;
  switch (block_.type) {
  case block_type::raw:
    if (buffer_.size() - pos_ < block_.block_size)
      return error::output_too_small;
    if (block_.block_size > 0)
      std::memcpy(buffer_.data() + pos_, payload, block_.block_size);
    pos_ += block_.block_size;
    break;
  case block_type::rle:
    if (buffer_.size() - pos_ < block_.block_size)
      return error::output_too_small;
    if (block_.block_size > 0)
      std::memset(buffer_.data() + pos_, std::to_integer<int>(payload[0]),
                  block_.block_size);
    pos_ += block_.block_size;
    break;
  case block_type::compressed: {
    const error err = decode_compressed_block(payload, block_.block_size,
                                              entropy_, buffer_.data(),
                                              buffer_.size(), pos_, block_max_);
    if (err != error::none)
      return err;
    break;
  }
  case block_type::reserved:
    return error::reserved_block_type;
  }

  if (header_.has_checksum && pos_ > before)
    hash_.update(buffer_.data() + before, pos_ - before);
  frame_written_ += pos_ - before;
  return error::none;
}

result<decode_status> decoder::decode(const std::byte *src,
                                      std::size_t src_size,
                                      std::size_t &src_pos, std::byte *dst,
                                      std::size_t dst_size,
                                      std::size_t &dst_pos) {
  for (;;) {
    if (!drain(dst, dst_size, dst_pos))
      return decode_status::output_full;

    switch (phase_) {
    case phase::frame_start: {
      if (!stage(src, src_size, src_pos, 4))
        return decode_status::need_input;
      const std::uint32_t magic = read_le32(staging_.data());
      if (is_skippable_magic(magic)) {
        if (!stage(src, src_size, src_pos, 8))
          return decode_status::need_input;
        skip_remaining_ = read_le32(staging_.data() + 4);
        staging_.clear();
        phase_ = phase::skip_payload;
        break;
      }
      if (magic != frame_magic)
        return error::bad_magic;
      if (!stage(src, src_size, src_pos, 5))
        return decode_status::need_input;
      const frame_header_descriptor desc{
          std::to_integer<std::uint8_t>(staging_[4])};
      if (desc.reserved_bit())
        return error::reserved_bit_set;
      const std::size_t total = 5 + (desc.single_segment() ? 0 : 1) +
                                desc.did_field_size() + desc.fcs_field_size();
      if (!stage(src, src_size, src_pos, total))
        return decode_status::need_input;
      std::size_t header_size = 0;
      const auto parsed = parse_frame_header(staging_.data() + 4,
                                             staging_.size() - 4, header_size);
      if (!parsed)
        return parsed.err();
      header_ = *parsed;
      staging_.clear();
      const error err = start_frame();
      if (err != error::none)
        return err;
      break;
    }

    case phase::skip_payload: {
      const std::size_t avail = src_size - src_pos;
      const std::size_t take = skip_remaining_ < avail
                                   ? static_cast<std::size_t>(skip_remaining_)
                                   : avail;
      src_pos += take;
      skip_remaining_ -= take;
      if (skip_remaining_ > 0)
        return decode_status::need_input;
      phase_ = phase::frame_finish;
      break;
    }

    case phase::block_start: {
      if (!stage(src, src_size, src_pos, block_header_size))
        return decode_status::need_input;
      const auto blk = parse_block_header(staging_.data(), staging_.size());
      if (!blk)
        return blk.err();
      staging_.clear();
      block_ = *blk;
      if (block_.block_size > block_max_)
        return error::block_too_large;
      payload_need_ = block_.type == block_type::rle ? 1 : block_.block_size;
      phase_ = phase::block_payload;
      break;
    }

    case phase::block_payload: {
      const std::byte *payload;
      if (staging_.empty() && src_size - src_pos >= payload_need_) {
        payload = src + src_pos;
        src_pos += payload_need_;
      } else {
        if (!stage(src, src_size, src_pos, payload_need_))
          return decode_status::need_input;
        payload = staging_.data();
      }
      const error err = process_block(payload);
      staging_.clear();
      if (err != error::none)
        return err;
      if (block_.last_block) {
        if (header_.content_size && frame_written_ != *header_.content_size)
          return error::content_size_mismatch;
        phase_ =
            header_.has_checksum ? phase::frame_checksum : phase::frame_finish;
      } else {
        phase_ = phase::block_start;
      }
      break;
    }

    case phase::frame_checksum: {
      if (!stage(src, src_size, src_pos, 4))
        return decode_status::need_input;
      const std::uint32_t stored = read_le32(staging_.data());
      staging_.clear();
      if (static_cast<std::uint32_t>(hash_.digest()) != stored)
        return error::checksum_mismatch;
      phase_ = phase::frame_finish;
      break;
    }

    case phase::frame_finish:
      phase_ = phase::frame_start;
      return decode_status::frame_complete;
    }
  }
}

void decoder::reset() {
  phase_ = phase::frame_start;
  staging_.clear();
  pos_ = 0;
  emit_ = 0;
}

} // namespace crunch
