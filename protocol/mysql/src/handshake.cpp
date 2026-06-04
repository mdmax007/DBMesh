#include "dbmesh/protocol/mysql/handshake.h"

#include "dbmesh/protocol/mysql/codec.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dbmesh::protocol::mysql {

// ── HandshakeV10 ──────────────────────────────────────────────────────────

void HandshakeV10::generate_auth_data() {
  if (RAND_bytes(auth_data.data(),
                 static_cast<int>(auth_data.size())) != 1) {
    throw std::runtime_error("RAND_bytes failed");
  }
  // Null bytes in auth_data would terminate the null-terminated field early.
  // Replace any 0x00 with 0x01.
  for (auto& b : auth_data) if (b == 0x00) b = 0x01;
}

std::vector<uint8_t> HandshakeV10::encode() const {
  std::vector<uint8_t> buf;
  buf.reserve(128);

  // Protocol version
  write_u8(buf, 0x0A);
  // Server version (null-terminated)
  write_null_str(buf, server_version);
  // Connection ID
  write_u32_le(buf, connection_id);
  // Auth-plugin-data part 1 (8 bytes)
  write_bytes(buf, auth_data.data(), 8);
  // Filler
  write_u8(buf, 0x00);
  // Capability flags (lower 2 bytes)
  write_u16_le(buf, static_cast<uint16_t>(capability_flags & 0xFFFF));
  // Character set
  write_u8(buf, character_set);
  // Status flags
  write_u16_le(buf, status_flags);
  // Capability flags (upper 2 bytes)
  write_u16_le(buf, static_cast<uint16_t>((capability_flags >> 16) & 0xFFFF));
  // Length of auth-plugin-data (= 21 for native_password: 20 data + 1 null)
  write_u8(buf, 21);
  // Reserved (10 zero bytes)
  for (int i = 0; i < 10; ++i) write_u8(buf, 0x00);
  // Auth-plugin-data part 2 (12 bytes: bytes 8-19 of auth_data, plus null)
  write_bytes(buf, auth_data.data() + 8, 12);
  write_u8(buf, 0x00);
  // Auth-plugin name (null-terminated)
  write_null_str(buf, auth_plugin_name);

  return buf;
}

Result<HandshakeV10, std::string>
HandshakeV10::decode(const std::vector<uint8_t>& payload) {
  if (payload.empty() || payload[0] != 0x0A)
    return Err(std::string("not a HandshakeV10 packet"));

  HandshakeV10 hs;
  std::size_t pos = 1;

  hs.server_version = read_null_str(payload.data(), payload.size(), &pos);
  if (pos + 4 > payload.size()) return Err(std::string("truncated handshake"));
  hs.connection_id = read_u32_le(payload.data() + pos); pos += 4;

  // auth-plugin-data part 1 (8 bytes)
  if (pos + 8 > payload.size()) return Err(std::string("truncated auth data 1"));
  for (int i = 0; i < 8; ++i) hs.auth_data[static_cast<std::size_t>(i)] = payload[pos++];
  pos += 1;  // filler 0x00

  if (pos + 2 > payload.size()) return Err(std::string("truncated caps low"));
  uint32_t cap_low = read_u16_le(payload.data() + pos); pos += 2;

  uint32_t cap_high = 0;
  uint8_t  auth_data_len = 0;
  if (pos < payload.size()) {
    hs.character_set = payload[pos++];
    if (pos + 2 <= payload.size()) { hs.status_flags = read_u16_le(payload.data() + pos); pos += 2; }
    if (pos + 2 <= payload.size()) { cap_high = read_u16_le(payload.data() + pos); pos += 2; }
    if (pos < payload.size()) auth_data_len = payload[pos++];
    pos += 10;  // reserved
  }
  hs.capability_flags = cap_low | (cap_high << 16);

  // auth-plugin-data part 2 (>= 13 bytes incl. trailing null; we read 12 salt bytes)
  if ((hs.capability_flags & caps::SECURE_CONNECTION) && pos < payload.size()) {
    int part2 = std::max(13, static_cast<int>(auth_data_len) - 8);
    int salt2 = std::min(12, part2 - 1);  // exclude trailing null
    for (int i = 0; i < salt2 && pos < payload.size(); ++i)
      hs.auth_data[static_cast<std::size_t>(8 + i)] = payload[pos++];
    // skip any remaining part2 bytes (incl. null)
    pos += static_cast<std::size_t>(part2 - salt2);
  }

  if ((hs.capability_flags & caps::PLUGIN_AUTH) && pos < payload.size())
    hs.auth_plugin_name = read_null_str(payload.data(), payload.size(), &pos);

  return Ok(std::move(hs));
}

// ── HandshakeResponse41 ───────────────────────────────────────────────────

std::vector<uint8_t> HandshakeResponse41::encode() const {
  std::vector<uint8_t> buf;
  buf.reserve(64 + username.size() + auth_response.size() + database.size());

  write_u32_le(buf, capability_flags);
  write_u32_le(buf, max_packet_size);
  write_u8(buf, character_set);
  for (int i = 0; i < 23; ++i) write_u8(buf, 0x00);  // reserved

  write_null_str(buf, username);

  // Auth response (length-prefixed; SECURE_CONNECTION style)
  write_u8(buf, static_cast<uint8_t>(auth_response.size()));
  write_bytes(buf, auth_response.data(), auth_response.size());

  if (capability_flags & caps::CONNECT_WITH_DB)
    write_null_str(buf, database);

  if (capability_flags & caps::PLUGIN_AUTH)
    write_null_str(buf, auth_plugin.empty() ? NATIVE_PASSWORD : auth_plugin);

  return buf;
}

Result<HandshakeResponse41, std::string>
HandshakeResponse41::decode(const std::vector<uint8_t>& payload) {
  if (payload.size() < 32)
    return Err(std::string("handshake response too short"));

  HandshakeResponse41 r;
  std::size_t pos = 0;

  r.capability_flags = read_u32_le(payload.data() + pos); pos += 4;
  r.max_packet_size  = read_u32_le(payload.data() + pos); pos += 4;
  r.character_set    = payload[pos++];
  pos += 23;  // reserved

  if (pos >= payload.size()) return Err(std::string("truncated response"));

  r.username = read_null_str(payload.data(), payload.size(), &pos);

  // Auth response: either length-encoded or length-prefixed depending on caps
  if (r.capability_flags & caps::PLUGIN_AUTH_LENENC) {
    auto len = read_lenenc_int(payload.data(), payload.size(), &pos);
    if (pos + len > payload.size())
      return Err(std::string("auth response overflows packet"));
    r.auth_response.assign(payload.begin() + static_cast<std::ptrdiff_t>(pos),
                            payload.begin() + static_cast<std::ptrdiff_t>(pos + len));
    pos += len;
  } else if (r.capability_flags & caps::SECURE_CONNECTION) {
    if (pos >= payload.size()) return Err(std::string("missing auth len byte"));
    uint8_t auth_len = payload[pos++];
    if (pos + auth_len > payload.size())
      return Err(std::string("auth response overflows packet"));
    r.auth_response.assign(payload.begin() + static_cast<std::ptrdiff_t>(pos),
                            payload.begin() + static_cast<std::ptrdiff_t>(pos + auth_len));
    pos += auth_len;
  }

  if ((r.capability_flags & caps::CONNECT_WITH_DB) && pos < payload.size())
    r.database = read_null_str(payload.data(), payload.size(), &pos);

  if ((r.capability_flags & caps::PLUGIN_AUTH) && pos < payload.size())
    r.auth_plugin = read_null_str(payload.data(), payload.size(), &pos);

  return Ok(std::move(r));
}

// ── mysql_native_password ─────────────────────────────────────────────────

namespace {

// Computes h3 = SHA1(salt + SHA1(SHA1(password))) and h1 = SHA1(password).
void native_hashes(const std::string& pwd, const std::array<uint8_t, 20>& salt,
                   std::array<uint8_t, 20>& h1, std::array<uint8_t, 20>& h3) {
  SHA1(reinterpret_cast<const unsigned char*>(pwd.data()), pwd.size(), h1.data());
  std::array<uint8_t, 20> h2{};
  SHA1(h1.data(), h1.size(), h2.data());
  std::vector<uint8_t> salted;
  salted.insert(salted.end(), salt.begin(), salt.end());
  salted.insert(salted.end(), h2.begin(), h2.end());
  SHA1(salted.data(), salted.size(), h3.data());
}

} // namespace

bool verify_native_password(const std::string& password_plaintext,
                             const std::array<uint8_t, 20>& auth_data,
                             const std::vector<uint8_t>& auth_response) {
  if (auth_response.size() != 20) return false;
  if (password_plaintext.empty()) {
    bool all_zero = true;
    for (auto b : auth_response) if (b) { all_zero = false; break; }
    return all_zero;
  }

  std::array<uint8_t, 20> h1{}, h3{};
  native_hashes(password_plaintext, auth_data, h1, h3);

  // expected SHA1(password) = auth_response XOR h3
  std::array<uint8_t, 20> expected{};
  for (int i = 0; i < 20; ++i)
    expected[static_cast<std::size_t>(i)] =
        auth_response[static_cast<std::size_t>(i)] ^ h3[static_cast<std::size_t>(i)];

  return expected == h1;
}

std::vector<uint8_t> scramble_native_password(
    const std::string& password_plaintext,
    const std::array<uint8_t, 20>& salt) {
  if (password_plaintext.empty()) return {};

  std::array<uint8_t, 20> h1{}, h3{};
  native_hashes(password_plaintext, salt, h1, h3);

  // token = SHA1(pwd) XOR SHA1(salt + SHA1(SHA1(pwd)))
  std::vector<uint8_t> token(20);
  for (int i = 0; i < 20; ++i)
    token[static_cast<std::size_t>(i)] =
        h1[static_cast<std::size_t>(i)] ^ h3[static_cast<std::size_t>(i)];
  return token;
}

} // namespace dbmesh::protocol::mysql
