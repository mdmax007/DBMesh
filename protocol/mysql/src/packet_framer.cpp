#include "dbmesh/protocol/mysql/packet_framer.h"

#include "dbmesh/protocol/mysql/codec.h"

#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

namespace dbmesh::protocol::mysql {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// ── Pure helpers ──────────────────────────────────────────────────────────

std::vector<uint8_t> PacketFramer::encode(const std::vector<uint8_t>& payload,
                                           uint8_t sequence) {
  std::vector<uint8_t> frame;
  frame.reserve(4 + payload.size());
  uint32_t len = static_cast<uint32_t>(payload.size());
  write_u24_le(frame, len);
  write_u8(frame, sequence);
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

Result<MySqlPacket, IoError> PacketFramer::decode(const uint8_t* data,
                                                    std::size_t size) {
  if (size < 4) return Err(IoError::READ_FAILED);
  uint32_t length   = read_u24_le(data);
  uint8_t  sequence = data[3];
  if (4 + length > size) return Err(IoError::READ_FAILED);
  return Ok(MySqlPacket{length, sequence,
                         std::vector<uint8_t>(data + 4, data + 4 + length)});
}

// ── Async I/O ─────────────────────────────────────────────────────────────

asio::awaitable<Result<MySqlPacket, IoError>>
PacketFramer::read_packet(tcp::socket& socket) {
  // Read 4-byte header
  std::array<uint8_t, 4> hdr{};
  boost::system::error_code ec;
  co_await asio::async_read(socket, asio::buffer(hdr),
                             asio::redirect_error(asio::use_awaitable, ec));
  if (ec) co_return Err(IoError::READ_FAILED);

  uint32_t length   = read_u24_le(hdr.data());
  uint8_t  sequence = hdr[3];

  if (length > MAX_PACKET_SIZE) co_return Err(IoError::PACKET_TOO_LARGE);

  std::vector<uint8_t> payload(length);
  if (length > 0) {
    co_await asio::async_read(socket, asio::buffer(payload),
                               asio::redirect_error(asio::use_awaitable, ec));
    if (ec) co_return Err(IoError::READ_FAILED);
  }

  // Multi-packet: chained packets have length == MAX_PACKET_SIZE.
  // The final packet has length < MAX_PACKET_SIZE.
  // This implementation accumulates all chunks into one logical packet.
  while (length == MAX_PACKET_SIZE) {
    co_await asio::async_read(socket, asio::buffer(hdr),
                               asio::redirect_error(asio::use_awaitable, ec));
    if (ec) co_return Err(IoError::READ_FAILED);

    uint32_t chunk_len = read_u24_le(hdr.data());
    sequence           = hdr[3];

    if (chunk_len > 0) {
      std::size_t old_size = payload.size();
      payload.resize(old_size + chunk_len);
      co_await asio::async_read(socket, asio::buffer(payload.data() + old_size, chunk_len),
                                 asio::redirect_error(asio::use_awaitable, ec));
      if (ec) co_return Err(IoError::READ_FAILED);
    }
    length = chunk_len;
  }

  co_return Ok(MySqlPacket{static_cast<uint32_t>(payload.size()),
                             sequence, std::move(payload)});
}

asio::awaitable<Result<std::monostate, IoError>>
PacketFramer::write_packet(tcp::socket& socket,
                            const std::vector<uint8_t>& payload,
                            uint8_t& seq) {
  boost::system::error_code ec;
  std::size_t offset = 0;

  do {
    std::size_t chunk = std::min(payload.size() - offset,
                                 static_cast<std::size_t>(MAX_PACKET_SIZE));
    std::array<uint8_t, 4> hdr{};
    hdr[0] = static_cast<uint8_t>(chunk & 0xFF);
    hdr[1] = static_cast<uint8_t>((chunk >> 8) & 0xFF);
    hdr[2] = static_cast<uint8_t>((chunk >> 16) & 0xFF);
    hdr[3] = seq++;

    std::array<asio::const_buffer, 2> bufs{asio::buffer(hdr),
                                            asio::buffer(payload.data() + offset, chunk)};
    co_await asio::async_write(socket, bufs,
                                asio::redirect_error(asio::use_awaitable, ec));
    if (ec) co_return Err(IoError::WRITE_FAILED);
    offset += chunk;
  } while (offset < payload.size() ||
           (payload.size() > 0 && payload.size() % MAX_PACKET_SIZE == 0));
  // Last condition: if size is exactly a multiple of MAX_PACKET_SIZE, send a
  // 0-byte terminal packet per the MySQL spec.

  co_return Ok(std::monostate{});
}

} // namespace dbmesh::protocol::mysql
