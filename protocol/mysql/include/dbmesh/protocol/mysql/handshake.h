#ifndef DBMESH_PROTOCOL_MYSQL_HANDSHAKE_H_
#define DBMESH_PROTOCOL_MYSQL_HANDSHAKE_H_

#include "dbmesh/core/result.h"
#include "dbmesh/protocol/mysql/constants.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dbmesh::protocol::mysql {

// ── Server → Client initial handshake (HandshakeV10) ─────────────────────

struct HandshakeV10 {
  std::string              server_version = "8.0.30-dbmesh";
  uint32_t                 connection_id  = 0;
  std::array<uint8_t, 20>  auth_data{};   // random challenge bytes
  uint32_t                 capability_flags = caps::SERVER_FLAGS;
  uint8_t                  character_set    = charset::UTF8MB4;
  uint16_t                 status_flags     = status::AUTOCOMMIT;
  std::string              auth_plugin_name = NATIVE_PASSWORD;

  // Generates 20 cryptographically random bytes into auth_data.
  void generate_auth_data();

  // Returns the encoded packet payload (without the 4-byte MySQL header).
  [[nodiscard]] std::vector<uint8_t> encode() const;

  // Client side: parse a HandshakeV10 sent by a real backend server.
  // Reassembles the 20-byte salt from auth-plugin-data parts 1 and 2.
  [[nodiscard]] static Result<HandshakeV10, std::string>
  decode(const std::vector<uint8_t>& payload);
};

// ── Client → Server handshake response (HandshakeResponse41) ─────────────

struct HandshakeResponse41 {
  uint32_t             capability_flags = 0;
  uint32_t             max_packet_size  = 0;
  uint8_t              character_set    = 0;
  std::string          username;
  std::vector<uint8_t> auth_response;
  std::string          database;
  std::string          auth_plugin;

  // Parses the client packet payload.
  [[nodiscard]] static Result<HandshakeResponse41, std::string>
  decode(const std::vector<uint8_t>& payload);

  // Client side: encode our response to send to a real backend server.
  [[nodiscard]] std::vector<uint8_t> encode() const;
};

// ── Auth verification ─────────────────────────────────────────────────────

// Verifies a mysql_native_password challenge response.
// password_plaintext — the backend password stored in config (M1.9 upgrades to bcrypt).
// auth_data         — 20-byte random challenge from HandshakeV10.
// auth_response     — 20-byte response from client.
[[nodiscard]] bool verify_native_password(
    const std::string& password_plaintext,
    const std::array<uint8_t, 20>& auth_data,
    const std::vector<uint8_t>& auth_response);

// Client side: compute the 20-byte mysql_native_password token to send to a
// backend, given the backend's salt. Returns empty vector for empty password.
//   token = SHA1(pwd) XOR SHA1(salt + SHA1(SHA1(pwd)))
[[nodiscard]] std::vector<uint8_t> scramble_native_password(
    const std::string& password_plaintext,
    const std::array<uint8_t, 20>& salt);

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_HANDSHAKE_H_
