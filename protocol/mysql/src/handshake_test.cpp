#include "dbmesh/protocol/mysql/handshake.h"
#include "dbmesh/protocol/mysql/constants.h"

#include <openssl/sha.h>

#include <gtest/gtest.h>

namespace dbmesh::protocol::mysql {

// ── HandshakeV10 encoding ─────────────────────────────────────────────────

TEST(HandshakeV10, EncodesProtocolVersion) {
  HandshakeV10 hs;
  hs.connection_id = 1;
  hs.auth_data.fill(0x41);  // 'A' × 20
  auto buf = hs.encode();

  ASSERT_FALSE(buf.empty());
  EXPECT_EQ(buf[0], 0x0A);  // protocol version
}

TEST(HandshakeV10, EncodesServerVersion) {
  HandshakeV10 hs;
  hs.server_version = "8.0.30-dbmesh";
  hs.connection_id  = 42;
  hs.auth_data.fill(0x01);
  auto buf = hs.encode();

  // Version string starts at offset 1, null-terminated
  std::string ver;
  for (std::size_t i = 1; i < buf.size() && buf[i] != 0x00; ++i)
    ver += static_cast<char>(buf[i]);
  EXPECT_EQ(ver, "8.0.30-dbmesh");
}

TEST(HandshakeV10, EncodesConnectionId) {
  HandshakeV10 hs;
  hs.server_version = "8.0.30";
  hs.connection_id  = 0xDEADCAFE;
  hs.auth_data.fill(0x01);
  auto buf = hs.encode();

  // Connection ID is at offset 1 + strlen(version) + 1 (null byte)
  std::size_t off = 1 + hs.server_version.size() + 1;
  ASSERT_GE(buf.size(), off + 4);
  uint32_t conn_id = static_cast<uint32_t>(buf[off]) |
                     (static_cast<uint32_t>(buf[off + 1]) << 8) |
                     (static_cast<uint32_t>(buf[off + 2]) << 16) |
                     (static_cast<uint32_t>(buf[off + 3]) << 24);
  EXPECT_EQ(conn_id, 0xDEADCAFEu);
}

TEST(HandshakeV10, AdvertisesRequiredCapabilities) {
  HandshakeV10 hs;
  hs.connection_id = 1;
  hs.auth_data.fill(0x01);
  auto buf = hs.encode();

  // Capability flags are at fixed offset after version + conn_id + 8 auth bytes + filler
  std::size_t cap_off = 1 + hs.server_version.size() + 1 + 4 + 8 + 1;
  ASSERT_GE(buf.size(), cap_off + 4);

  uint32_t lower = static_cast<uint32_t>(buf[cap_off]) |
                   (static_cast<uint32_t>(buf[cap_off + 1]) << 8);
  // After status + reserved, upper 2 bytes are at cap_off + 2 + 1 + 2 = cap_off + 5
  std::size_t upper_off = cap_off + 2 + 1 + 2;  // +charset +status
  ASSERT_GE(buf.size(), upper_off + 2);
  uint32_t upper = static_cast<uint32_t>(buf[upper_off]) |
                   (static_cast<uint32_t>(buf[upper_off + 1]) << 8);
  uint32_t cap_flags = lower | (upper << 16);

  EXPECT_TRUE(cap_flags & caps::PROTOCOL_41);
  EXPECT_TRUE(cap_flags & caps::PLUGIN_AUTH);
  EXPECT_TRUE(cap_flags & caps::SECURE_CONNECTION);
}

TEST(HandshakeV10, EndsWithPluginName) {
  HandshakeV10 hs;
  hs.connection_id     = 1;
  hs.auth_plugin_name  = NATIVE_PASSWORD;
  hs.auth_data.fill(0x01);
  auto buf = hs.encode();

  // Auth plugin name is at the end (null-terminated)
  ASSERT_GE(buf.size(), strlen(NATIVE_PASSWORD) + 1);
  std::size_t name_start = buf.size() - strlen(NATIVE_PASSWORD) - 1;
  std::string name;
  for (std::size_t i = name_start; buf[i] != 0x00; ++i)
    name += static_cast<char>(buf[i]);
  EXPECT_EQ(name, NATIVE_PASSWORD);
}

TEST(HandshakeV10, GeneratesNonZeroAuthData) {
  HandshakeV10 hs;
  hs.connection_id = 1;
  hs.generate_auth_data();
  auto buf = hs.encode();
  ASSERT_FALSE(buf.empty());

  // auth_data should not be all zeros (probability 1 / 2^160)
  bool all_zero = true;
  for (auto b : hs.auth_data) if (b) { all_zero = false; break; }
  EXPECT_FALSE(all_zero);
  // No null bytes (replaced by 0x01)
  for (auto b : hs.auth_data) EXPECT_NE(b, 0x00);
}

// ── HandshakeResponse41 parsing ───────────────────────────────────────────

TEST(HandshakeResponse41, ParsesUsernameAndDatabase) {
  // Build a minimal valid client response
  std::vector<uint8_t> payload;
  // capability flags (u32): PROTOCOL_41 | SECURE_CONNECTION | CONNECT_WITH_DB
  uint32_t caps_val = caps::PROTOCOL_41 | caps::SECURE_CONNECTION |
                      caps::CONNECT_WITH_DB | caps::PLUGIN_AUTH;
  auto push_u32 = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) payload.push_back(static_cast<uint8_t>((v >> (8*i)) & 0xFF));
  };
  push_u32(caps_val);
  push_u32(0x01000000);  // max_packet_size
  payload.push_back(0x21);   // charset
  for (int i = 0; i < 23; ++i) payload.push_back(0x00);  // reserved
  // username: "app\0"
  for (char c : std::string("app")) payload.push_back(static_cast<uint8_t>(c));
  payload.push_back(0x00);
  // auth response: 20 bytes
  payload.push_back(20);  // length byte (SECURE_CONNECTION style)
  for (int i = 0; i < 20; ++i) payload.push_back(static_cast<uint8_t>(i));
  // database: "billing\0"
  for (char c : std::string("billing")) payload.push_back(static_cast<uint8_t>(c));
  payload.push_back(0x00);
  // plugin name: "mysql_native_password\0"
  for (char c : std::string(NATIVE_PASSWORD)) payload.push_back(static_cast<uint8_t>(c));
  payload.push_back(0x00);

  auto result = HandshakeResponse41::decode(payload);
  ASSERT_TRUE(is_ok(result)) << get_error(result);
  const auto& resp = get_value(result);

  EXPECT_EQ(resp.username,    "app");
  EXPECT_EQ(resp.database,    "billing");
  EXPECT_EQ(resp.auth_plugin, NATIVE_PASSWORD);
  EXPECT_EQ(resp.auth_response.size(), 20u);
}

TEST(HandshakeResponse41, RejectsTooShortPayload) {
  std::vector<uint8_t> payload = {0x01, 0x02};  // too short
  auto result = HandshakeResponse41::decode(payload);
  EXPECT_TRUE(is_err(result));
}

// ── mysql_native_password ─────────────────────────────────────────────────

TEST(NativePassword, AcceptsCorrectPassword) {
  std::array<uint8_t, 20> salt{};
  for (int i = 0; i < 20; ++i) salt[static_cast<std::size_t>(i)] = static_cast<uint8_t>(i + 1);

  // Compute the expected response for password "secret"
  // SHA1("secret") XOR SHA1(salt + SHA1(SHA1("secret")))
  // We test by computing the expected token using the same algorithm.
  // Here we just verify that a known-good token verifies correctly.
  // (A separate negative test ensures wrong password fails.)

  // Build correct token
  std::array<uint8_t, 20> h1{};
  SHA1(reinterpret_cast<const unsigned char*>("secret"), 6, h1.data());
  std::array<uint8_t, 20> h2{};
  SHA1(h1.data(), 20, h2.data());
  std::vector<uint8_t> salted(salt.begin(), salt.end());
  salted.insert(salted.end(), h2.begin(), h2.end());
  std::array<uint8_t, 20> h3{};
  SHA1(salted.data(), salted.size(), h3.data());
  std::vector<uint8_t> token(20);
  for (int i = 0; i < 20; ++i)
    token[static_cast<std::size_t>(i)] = h1[static_cast<std::size_t>(i)] ^ h3[static_cast<std::size_t>(i)];

  EXPECT_TRUE(verify_native_password("secret", salt, token));
}

TEST(NativePassword, RejectsWrongPassword) {
  std::array<uint8_t, 20> salt{};
  for (int i = 0; i < 20; ++i) salt[static_cast<std::size_t>(i)] = static_cast<uint8_t>(i + 1);

  // Compute token for "secret" but verify against "wrong"
  std::array<uint8_t, 20> h1{};
  SHA1(reinterpret_cast<const unsigned char*>("secret"), 6, h1.data());
  std::array<uint8_t, 20> h2{};
  SHA1(h1.data(), 20, h2.data());
  std::vector<uint8_t> salted(salt.begin(), salt.end());
  salted.insert(salted.end(), h2.begin(), h2.end());
  std::array<uint8_t, 20> h3{};
  SHA1(salted.data(), salted.size(), h3.data());
  std::vector<uint8_t> token(20);
  for (int i = 0; i < 20; ++i)
    token[static_cast<std::size_t>(i)] = h1[static_cast<std::size_t>(i)] ^ h3[static_cast<std::size_t>(i)];

  EXPECT_FALSE(verify_native_password("wrong", salt, token));
}

TEST(NativePassword, RejectsWrongLengthToken) {
  std::array<uint8_t, 20> salt{};
  std::vector<uint8_t> bad_token(10, 0xAA);
  EXPECT_FALSE(verify_native_password("secret", salt, bad_token));
}

} // namespace dbmesh::protocol::mysql
