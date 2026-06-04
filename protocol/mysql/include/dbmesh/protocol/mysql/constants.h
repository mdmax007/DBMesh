#ifndef DBMESH_PROTOCOL_MYSQL_CONSTANTS_H_
#define DBMESH_PROTOCOL_MYSQL_CONSTANTS_H_

#include <cstdint>

namespace dbmesh::protocol::mysql {

// ── Capability flags (CLIENT_*) ───────────────────────────────────────────
namespace caps {
constexpr uint32_t LONG_PASSWORD            = 0x00000001;
constexpr uint32_t FOUND_ROWS               = 0x00000002;
constexpr uint32_t LONG_FLAG                = 0x00000004;
constexpr uint32_t CONNECT_WITH_DB          = 0x00000008;
constexpr uint32_t NO_SCHEMA                = 0x00000010;
constexpr uint32_t PROTOCOL_41              = 0x00000200;
constexpr uint32_t INTERACTIVE              = 0x00000400;
constexpr uint32_t SSL                      = 0x00000800;
constexpr uint32_t TRANSACTIONS             = 0x00002000;
constexpr uint32_t SECURE_CONNECTION        = 0x00008000;
constexpr uint32_t MULTI_STATEMENTS         = 0x00010000;
constexpr uint32_t MULTI_RESULTS            = 0x00020000;
constexpr uint32_t PS_MULTI_RESULTS         = 0x00040000;
constexpr uint32_t PLUGIN_AUTH              = 0x00080000;
constexpr uint32_t CONNECT_ATTRS            = 0x00100000;
constexpr uint32_t PLUGIN_AUTH_LENENC       = 0x00200000;
constexpr uint32_t CAN_HANDLE_EXPIRED       = 0x00400000;
constexpr uint32_t SESSION_TRACK            = 0x00800000;
constexpr uint32_t DEPRECATE_EOF            = 0x01000000;

// Flags advertised by DBMesh to clients.
// NOTE: DEPRECATE_EOF is intentionally NOT advertised in M1.2 — we emit
// classic EOF packets (0xFE) as result-set separators. M1.2.6 adds full
// DEPRECATE_EOF support in the result-set proxy.
constexpr uint32_t SERVER_FLAGS =
    LONG_PASSWORD | FOUND_ROWS | LONG_FLAG | CONNECT_WITH_DB |
    PROTOCOL_41  | TRANSACTIONS | SECURE_CONNECTION |
    MULTI_STATEMENTS | MULTI_RESULTS | PS_MULTI_RESULTS |
    PLUGIN_AUTH;
} // namespace caps

// ── Command bytes (COM_*) ─────────────────────────────────────────────────
namespace cmd {
constexpr uint8_t SLEEP        = 0x00;
constexpr uint8_t QUIT         = 0x01;
constexpr uint8_t INIT_DB      = 0x02;
constexpr uint8_t QUERY        = 0x03;
constexpr uint8_t FIELD_LIST   = 0x04;
constexpr uint8_t PING         = 0x0E;
constexpr uint8_t STATISTICS   = 0x09;
constexpr uint8_t SET_OPTION   = 0x1B;
constexpr uint8_t STMT_PREPARE = 0x16;
constexpr uint8_t STMT_EXECUTE = 0x17;
constexpr uint8_t STMT_CLOSE   = 0x19;
constexpr uint8_t STMT_RESET   = 0x1A;
} // namespace cmd

// ── Status flags ──────────────────────────────────────────────────────────
namespace status {
constexpr uint16_t IN_TRANSACTION    = 0x0001;
constexpr uint16_t AUTOCOMMIT        = 0x0002;
constexpr uint16_t MORE_RESULTS      = 0x0008;
constexpr uint16_t NO_GOOD_INDEX     = 0x0010;
constexpr uint16_t NO_INDEX          = 0x0020;
constexpr uint16_t CURSOR_EXISTS     = 0x0040;
constexpr uint16_t LAST_ROW_SENT     = 0x0080;
} // namespace status

// ── Column types ──────────────────────────────────────────────────────────
namespace col_type {
constexpr uint8_t DECIMAL     = 0x00;
constexpr uint8_t TINY        = 0x01;
constexpr uint8_t SHORT       = 0x02;
constexpr uint8_t LONG        = 0x03;
constexpr uint8_t FLOAT       = 0x04;
constexpr uint8_t DOUBLE      = 0x05;
constexpr uint8_t LONGLONG    = 0x08;
constexpr uint8_t VARCHAR     = 0x0F;
constexpr uint8_t VAR_STRING  = 0xFD;
constexpr uint8_t STRING      = 0xFE;
} // namespace col_type

// ── Charset codes ─────────────────────────────────────────────────────────
namespace charset {
constexpr uint8_t UTF8MB4 = 0x2D;  // 45
constexpr uint8_t BINARY  = 0x3F;  // 63
} // namespace charset

// ── Packet header bytes ───────────────────────────────────────────────────
constexpr uint8_t OK_PACKET  = 0x00;
constexpr uint8_t ERR_PACKET = 0xFF;
constexpr uint8_t EOF_PACKET = 0xFE;

// ── Auth plugins ──────────────────────────────────────────────────────────
constexpr const char* NATIVE_PASSWORD  = "mysql_native_password";
constexpr const char* CACHING_SHA2     = "caching_sha2_password";

// ── MySQL error codes ─────────────────────────────────────────────────────
namespace err {
constexpr uint16_t CON_COUNT_ERROR      = 1040;
constexpr uint16_t DBACCESS_DENIED      = 1044;
constexpr uint16_t ACCESS_DENIED        = 1045;
constexpr uint16_t NOT_ALLOWED_COMMAND  = 1148;
constexpr uint16_t UNKNOWN_COM_ERROR    = 1047;
constexpr uint16_t NO_DB_ERROR          = 1046;
} // namespace err

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_CONSTANTS_H_
