#ifndef DBMESH_PROTOCOL_MYSQL_PACKET_FRAMER_H_
#define DBMESH_PROTOCOL_MYSQL_PACKET_FRAMER_H_

#include "dbmesh/core/result.h"
#include "dbmesh/protocol/mysql/types.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <vector>

namespace dbmesh::protocol::mysql {

// Reads and writes MySQL wire-protocol framing.
// Each packet: 3-byte LE length | 1-byte sequence | payload.
// Packets ≥ 16 MB are split into chained packets (length = 0xFFFFFF).
class PacketFramer {
 public:
  // Reads one complete logical packet (handles chained multi-packets).
  boost::asio::awaitable<Result<MySqlPacket, IoError>>
  read_packet(boost::asio::ip::tcp::socket& socket);

  // Writes payload as one or more MySQL packets, incrementing seq.
  boost::asio::awaitable<Result<std::monostate, IoError>>
  write_packet(boost::asio::ip::tcp::socket& socket,
               const std::vector<uint8_t>& payload,
               uint8_t& seq);

  // ── Pure encoding helpers (testable without I/O) ──────────────────────
  static std::vector<uint8_t> encode(const std::vector<uint8_t>& payload,
                                      uint8_t sequence);
  static Result<MySqlPacket, IoError> decode(const uint8_t* data,
                                              std::size_t size);
};

constexpr uint32_t MAX_PACKET_SIZE = 0xFFFFFF;  // 16 MB - 1

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_PACKET_FRAMER_H_
