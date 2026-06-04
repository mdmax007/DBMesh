#include "dbmesh/protocol/mysql/codec.h"
#include "dbmesh/protocol/mysql/packet_framer.h"

#include <gtest/gtest.h>

namespace dbmesh::protocol::mysql {

// ── Codec helpers ─────────────────────────────────────────────────────────

TEST(Codec, WriteReadU16LE) {
  std::vector<uint8_t> buf;
  write_u16_le(buf, 0xABCD);
  ASSERT_EQ(buf.size(), 2u);
  EXPECT_EQ(buf[0], 0xCD);
  EXPECT_EQ(buf[1], 0xAB);
  EXPECT_EQ(read_u16_le(buf.data()), 0xABCD);
}

TEST(Codec, WriteReadU32LE) {
  std::vector<uint8_t> buf;
  write_u32_le(buf, 0xDEADBEEF);
  ASSERT_EQ(buf.size(), 4u);
  EXPECT_EQ(read_u32_le(buf.data()), 0xDEADBEEFu);
}

TEST(Codec, WriteReadU24LE) {
  std::vector<uint8_t> buf;
  write_u24_le(buf, 0x123456);
  ASSERT_EQ(buf.size(), 3u);
  EXPECT_EQ(buf[0], 0x56);
  EXPECT_EQ(buf[1], 0x34);
  EXPECT_EQ(buf[2], 0x12);
  EXPECT_EQ(read_u24_le(buf.data()), 0x123456u);
}

TEST(Codec, LengthEncodedInt_OneByte) {
  std::vector<uint8_t> buf;
  write_lenenc_int(buf, 42);
  ASSERT_EQ(buf.size(), 1u);
  std::size_t pos = 0;
  EXPECT_EQ(read_lenenc_int(buf.data(), buf.size(), &pos), 42u);
  EXPECT_EQ(pos, 1u);
}

TEST(Codec, LengthEncodedInt_TwoBytes) {
  std::vector<uint8_t> buf;
  write_lenenc_int(buf, 300);
  ASSERT_EQ(buf.size(), 3u);  // 0xFC + 2 bytes
  EXPECT_EQ(buf[0], 0xFC);
  std::size_t pos = 0;
  EXPECT_EQ(read_lenenc_int(buf.data(), buf.size(), &pos), 300u);
}

TEST(Codec, LengthEncodedInt_ThreeBytes) {
  std::vector<uint8_t> buf;
  write_lenenc_int(buf, 70000);
  ASSERT_EQ(buf.size(), 4u);  // 0xFD + 3 bytes
  EXPECT_EQ(buf[0], 0xFD);
  std::size_t pos = 0;
  EXPECT_EQ(read_lenenc_int(buf.data(), buf.size(), &pos), 70000u);
}

TEST(Codec, LengthEncodedString) {
  std::vector<uint8_t> buf;
  write_lenenc_str(buf, "hello");
  ASSERT_EQ(buf.size(), 6u);  // 1 byte length + 5 bytes data
  EXPECT_EQ(buf[0], 5);
  std::size_t pos = 0;
  EXPECT_EQ(read_lenenc_str(buf.data(), buf.size(), &pos), "hello");
  EXPECT_EQ(pos, 6u);
}

TEST(Codec, NullTerminatedString) {
  std::vector<uint8_t> buf;
  write_null_str(buf, "world");
  ASSERT_EQ(buf.size(), 6u);  // 5 bytes + null
  EXPECT_EQ(buf[5], 0x00);
  std::size_t pos = 0;
  EXPECT_EQ(read_null_str(buf.data(), buf.size(), &pos), "world");
  EXPECT_EQ(pos, 6u);
}

// ── PacketFramer pure encode/decode ───────────────────────────────────────

TEST(PacketFramer, EncodeDecodeRoundTrip) {
  std::vector<uint8_t> payload = {0x03, 0x53, 0x45, 0x4C, 0x45, 0x43, 0x54};
  uint8_t seq = 5;
  auto frame = PacketFramer::encode(payload, seq);

  ASSERT_GE(frame.size(), 4u);
  // Header: length = 7 (0x07, 0x00, 0x00), sequence = 5
  EXPECT_EQ(frame[0], 0x07);
  EXPECT_EQ(frame[1], 0x00);
  EXPECT_EQ(frame[2], 0x00);
  EXPECT_EQ(frame[3], 0x05);
  EXPECT_EQ(frame.size(), 11u);

  auto result = PacketFramer::decode(frame.data(), frame.size());
  ASSERT_TRUE(is_ok(result));
  const auto& pkt = get_value(result);
  EXPECT_EQ(pkt.length, 7u);
  EXPECT_EQ(pkt.sequence, 5u);
  EXPECT_EQ(pkt.payload, payload);
}

TEST(PacketFramer, EncodeEmptyPayload) {
  auto frame = PacketFramer::encode({}, 0);
  ASSERT_EQ(frame.size(), 4u);
  EXPECT_EQ(frame[0], 0x00);
  EXPECT_EQ(frame[1], 0x00);
  EXPECT_EQ(frame[2], 0x00);
  EXPECT_EQ(frame[3], 0x00);

  auto result = PacketFramer::decode(frame.data(), frame.size());
  ASSERT_TRUE(is_ok(result));
  EXPECT_EQ(get_value(result).payload.size(), 0u);
}

TEST(PacketFramer, DecodeFailsOnTruncated) {
  std::array<uint8_t, 3> short_hdr = {0xFF, 0xFF, 0xFF};  // too short
  auto result = PacketFramer::decode(short_hdr.data(), short_hdr.size());
  EXPECT_TRUE(is_err(result));
}

TEST(PacketFramer, SequenceRollover) {
  for (uint32_t i = 0; i <= 256; ++i) {
    uint8_t seq = static_cast<uint8_t>(i & 0xFF);
    auto frame = PacketFramer::encode({0x01}, seq);
    auto result = PacketFramer::decode(frame.data(), frame.size());
    ASSERT_TRUE(is_ok(result));
    EXPECT_EQ(get_value(result).sequence, seq);
  }
}

} // namespace dbmesh::protocol::mysql
