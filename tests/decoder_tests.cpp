#include "check.hpp"

#include <crunch/decoder.hpp>
#include <crunch/dictionary.hpp>

#include <cstring>
#include <vector>

using test::bytes;

namespace {

std::string data_dir;

struct stream_result {
  crunch::error err = crunch::error::none;
  std::vector<std::byte> output;
  std::size_t frames = 0;
};

stream_result run_stream(crunch::decoder &dec, const std::vector<std::byte> &in,
                         std::size_t in_chunk, std::size_t out_chunk) {
  stream_result out;
  std::vector<std::byte> window(out_chunk);
  std::size_t src_pos = 0;
  std::size_t fed = 0;
  for (;;) {
    const std::size_t last_fed = fed;
    const std::size_t last_src = src_pos;
    const std::size_t last_out = out.output.size();
    const std::size_t last_frames = out.frames;
    if (fed < in.size()) {
      fed += in_chunk;
      if (fed > in.size())
        fed = in.size();
    }
    std::size_t dst_pos = 0;
    auto r = dec.decode(in.data(), fed, src_pos, window.data(), window.size(),
                        dst_pos);
    out.output.insert(out.output.end(), window.begin(),
                      window.begin() + dst_pos);
    if (!r) {
      out.err = r.err();
      return out;
    }
    switch (*r) {
    case crunch::decode_status::frame_complete:
      ++out.frames;
      if (src_pos == in.size() && fed == in.size())
        return out;
      break;
    case crunch::decode_status::need_input:
      if (fed == in.size()) {
        out.err = crunch::error::truncated_input;
        return out;
      }
      break;
    case crunch::decode_status::output_full:
      break;
    }
    // a pass without any progress means the decoder is stuck
    if (fed == last_fed && src_pos == last_src &&
        out.output.size() == last_out && out.frames == last_frames)
      return out;
  }
}

void check_stream(const std::string &zst_name,
                  const std::vector<std::byte> &expect,
                  const crunch::dictionary *dict = nullptr) {
  const auto frame = test::read_file(data_dir + "/" + zst_name);
  CHECK(!frame.empty());
  if (frame.empty())
    return;

  const std::size_t whole_out = expect.empty() ? 1 : expect.size();
  const std::size_t chunks[][2] = {{frame.size(), whole_out},
                                   {1, 1},
                                   {7, 3},
                                   {frame.size(), 1},
                                   {1, whole_out}};
  for (const auto &c : chunks) {
    crunch::decoder dec;
    dec.dict = dict;
    const stream_result r = run_stream(dec, frame, c[0], c[1]);
    CHECK(r.err == crunch::error::none);
    CHECK(r.frames == 1);
    CHECK(r.output.size() == expect.size());
    CHECK(r.output == expect);
  }
}

void test_raw_block() {
  check_stream("random.zst", test::read_file(data_dir + "/random.bin"));
}

void test_rle_block() {
  check_stream("rle.zst", test::read_file(data_dir + "/rle.bin"));
}

// same compressed block behind single segment and window descriptor headers
void test_compressed_block() {
  const auto original = test::read_file(data_dir + "/hello.txt");
  CHECK(original.size() == 50);
  check_stream("hello.zst", original);
  check_stream("hello_piped.zst", original);
}

// 512 KB under a 1 KB window, hundreds of blocks and constant buffer slides
void test_sliding_window() {
  const auto original = test::read_file(data_dir + "/window.bin");
  const auto frame = test::read_file(data_dir + "/window.zst");
  CHECK(original.size() == 512 * 1024);
  CHECK(!frame.empty());
  if (original.empty() || frame.empty())
    return;

  const std::size_t chunks[][2] = {
      {frame.size(), original.size()}, {1, 1021}, {4093, 1}, {997, 997}};
  for (const auto &c : chunks) {
    crunch::decoder dec;
    const stream_result r = run_stream(dec, frame, c[0], c[1]);
    CHECK(r.err == crunch::error::none);
    CHECK(r.frames == 1);
    CHECK(r.output == original);
  }
}

void test_dictionary_frame() {
  const auto dict_file = test::read_file(data_dir + "/hello.dict");
  CHECK(!dict_file.empty());
  if (dict_file.empty())
    return;
  crunch::dictionary dict;
  CHECK(crunch::parse_dictionary(dict_file.data(), dict_file.size(), dict) ==
        crunch::error::none);
  check_stream("hello_dict.zst", test::read_file(data_dir + "/hello.txt"),
               &dict);

  // the frame names dictionary 3000
  crunch::decoder dec;
  const auto frame = test::read_file(data_dir + "/hello_dict.zst");
  const stream_result r = run_stream(dec, frame, frame.size(), 64);
  CHECK(r.err == crunch::error::wrong_dictionary);
}

void test_multi_frame() {
  auto stream = test::read_file(data_dir + "/random.zst");
  const auto second = test::read_file(data_dir + "/rle.zst");
  stream.insert(stream.end(), second.begin(), second.end());
  auto expect = test::read_file(data_dir + "/random.bin");
  const auto tail = test::read_file(data_dir + "/rle.bin");
  expect.insert(expect.end(), tail.begin(), tail.end());

  for (const std::size_t chunk : {stream.size(), std::size_t{5}}) {
    crunch::decoder dec;
    const stream_result r = run_stream(dec, stream, chunk, 512);
    CHECK(r.err == crunch::error::none);
    CHECK(r.frames == 2);
    CHECK(r.output == expect);
  }
}

void test_skippable_frame() {
  const unsigned char skip[] = {0x50, 0x2a, 0x4d, 0x18, 0x04, 0x00,
                                0x00, 0x00, 0xde, 0xad, 0xbe, 0xef};
  std::vector<std::byte> stream(bytes(skip), bytes(skip) + sizeof(skip));
  const auto second = test::read_file(data_dir + "/hello.zst");
  stream.insert(stream.end(), second.begin(), second.end());

  for (const std::size_t chunk : {stream.size(), std::size_t{1}}) {
    crunch::decoder dec;
    const stream_result r = run_stream(dec, stream, chunk, 64);
    CHECK(r.err == crunch::error::none);
    CHECK(r.frames == 2);
    CHECK(r.output.size() == 50);
  }
}

// hello_piped.zst carries a window descriptor well above 1024 bytes
// hello.zst is single-segment with a 50-byte window
void test_window_limit() {
  const auto piped = test::read_file(data_dir + "/hello_piped.zst");
  crunch::decoder dec;
  dec.window_limit = 1024;
  stream_result r = run_stream(dec, piped, piped.size(), 64);
  CHECK(r.err == crunch::error::window_too_large);

  const auto plain = test::read_file(data_dir + "/hello.zst");
  crunch::decoder small;
  small.window_limit = 1024;
  r = run_stream(small, plain, plain.size(), 64);
  CHECK(r.err == crunch::error::none);
  CHECK(r.frames == 1);
}

void test_errors() {
  auto frame = test::read_file(data_dir + "/random.zst");
  CHECK(!frame.empty());
  if (frame.empty())
    return;

  {
    auto truncated = frame;
    truncated.pop_back();
    crunch::decoder dec;
    const stream_result r = run_stream(dec, truncated, 1, 64);
    CHECK(r.err == crunch::error::truncated_input);
  }
  {
    auto corrupt = frame;
    corrupt.back() ^= std::byte{0xFF};
    crunch::decoder dec;
    const stream_result r = run_stream(dec, corrupt, corrupt.size(), 64);
    CHECK(r.err == crunch::error::checksum_mismatch);
  }
  {
    const unsigned char junk[] = {0x11, 0x22, 0x33, 0x44};
    const std::vector<std::byte> stream(bytes(junk), bytes(junk) + 4);
    crunch::decoder dec;
    const stream_result r = run_stream(dec, stream, 4, 64);
    CHECK(r.err == crunch::error::bad_magic);
  }
}

// reset drops a partial frame so the decoder can take a fresh stream
void test_reset() {
  const auto frame = test::read_file(data_dir + "/hello.zst");
  const auto original = test::read_file(data_dir + "/hello.txt");
  CHECK(!frame.empty());
  if (frame.empty())
    return;

  crunch::decoder dec;
  std::size_t src_pos = 0;
  std::size_t dst_pos = 0;
  std::vector<std::byte> out(64);
  auto r =
      dec.decode(frame.data(), 10, src_pos, out.data(), out.size(), dst_pos);
  CHECK(r);
  CHECK(*r == crunch::decode_status::need_input);

  dec.reset();
  const auto other = test::read_file(data_dir + "/random.zst");
  const stream_result full = run_stream(dec, other, other.size(), 512);
  CHECK(full.err == crunch::error::none);
  CHECK(full.frames == 1);
  CHECK(full.output == test::read_file(data_dir + "/random.bin"));
}

} // namespace

int main(int argc, char **argv) {
  data_dir = argc > 1 ? argv[1] : "tests/data";
  test_raw_block();
  test_rle_block();
  test_compressed_block();
  test_sliding_window();
  test_dictionary_frame();
  test_multi_frame();
  test_skippable_frame();
  test_window_limit();
  test_errors();
  test_reset();
  return test::report("decoder");
}
