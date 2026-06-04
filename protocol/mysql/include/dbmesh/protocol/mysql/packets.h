#ifndef DBMESH_PROTOCOL_MYSQL_PACKETS_H_
#define DBMESH_PROTOCOL_MYSQL_PACKETS_H_

#include "dbmesh/protocol/mysql/codec.h"
#include "dbmesh/protocol/mysql/constants.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dbmesh::protocol::mysql {

// ── OK packet ─────────────────────────────────────────────────────────────
inline std::vector<uint8_t> make_ok(uint64_t affected_rows   = 0,
                                     uint64_t last_insert_id  = 0,
                                     uint16_t status_flags    = status::AUTOCOMMIT,
                                     uint16_t warnings        = 0) {
  std::vector<uint8_t> buf;
  buf.reserve(12);
  write_u8(buf, OK_PACKET);
  write_lenenc_int(buf, affected_rows);
  write_lenenc_int(buf, last_insert_id);
  write_u16_le(buf, status_flags);
  write_u16_le(buf, warnings);
  return buf;
}

// ── ERR packet ────────────────────────────────────────────────────────────
inline std::vector<uint8_t> make_err(uint16_t    error_code,
                                      std::string_view sql_state,
                                      std::string_view message) {
  std::vector<uint8_t> buf;
  buf.reserve(9 + message.size());
  write_u8(buf, ERR_PACKET);
  write_u16_le(buf, error_code);
  write_u8(buf, '#');
  // SQL state is exactly 5 chars; pad or truncate
  std::string state(sql_state.substr(0, 5));
  while (state.size() < 5) state += '0';
  buf.insert(buf.end(), state.begin(), state.end());
  buf.insert(buf.end(), message.begin(), message.end());
  return buf;
}

// ── EOF / OK used as EOF ───────────────────────────────────────────────────
// When CLIENT_DEPRECATE_EOF is negotiated, use make_ok() in place of EOF.
// This builder is for connections that did NOT negotiate DEPRECATE_EOF.
inline std::vector<uint8_t> make_eof(uint16_t warnings     = 0,
                                      uint16_t status_flags = status::AUTOCOMMIT) {
  std::vector<uint8_t> buf;
  buf.reserve(5);
  write_u8(buf, EOF_PACKET);
  write_u16_le(buf, warnings);
  write_u16_le(buf, status_flags);
  return buf;
}

// ── Text result set helpers ───────────────────────────────────────────────

// Column count packet (length-encoded integer).
inline std::vector<uint8_t> make_column_count(uint64_t count) {
  std::vector<uint8_t> buf;
  write_lenenc_int(buf, count);
  return buf;
}

// Column definition packet (text protocol, MySQL 4.1+ format).
inline std::vector<uint8_t> make_column_def(std::string_view col_name,
                                              uint8_t  col_type   = col_type::VAR_STRING,
                                              uint32_t col_length  = 255,
                                              uint16_t col_flags   = 0x0000,
                                              uint8_t  col_charset = charset::UTF8MB4) {
  std::vector<uint8_t> buf;
  write_lenenc_str(buf, "def");     // catalog
  write_lenenc_str(buf, "");        // schema
  write_lenenc_str(buf, "");        // table
  write_lenenc_str(buf, "");        // org_table
  write_lenenc_str(buf, col_name);  // name
  write_lenenc_str(buf, "");        // org_name
  write_u8(buf, 0x0C);              // fixed-length field block length
  write_u8(buf, col_charset);       // charset low byte
  write_u8(buf, 0x00);              // charset high byte
  write_u32_le(buf, col_length);
  write_u8(buf, col_type);
  write_u16_le(buf, col_flags);
  write_u8(buf, 0x00);              // decimals
  write_u16_le(buf, 0x0000);        // filler
  return buf;
}

// One row of text-protocol data.
inline std::vector<uint8_t> make_text_row(
    const std::vector<std::string>& values) {
  std::vector<uint8_t> buf;
  for (const auto& v : values) {
    if (v.empty()) {
      write_u8(buf, 0xFB);  // NULL
    } else {
      write_lenenc_str(buf, v);
    }
  }
  return buf;
}

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_PACKETS_H_
