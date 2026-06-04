#ifndef DBMESH_PROTOCOL_MYSQL_CODEC_H_
#define DBMESH_PROTOCOL_MYSQL_CODEC_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace dbmesh::protocol::mysql {

// ── Write helpers ─────────────────────────────────────────────────────────

inline void write_u8(std::vector<uint8_t>& buf, uint8_t v) {
  buf.push_back(v);
}

inline void write_u16_le(std::vector<uint8_t>& buf, uint16_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void write_u24_le(std::vector<uint8_t>& buf, uint32_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
}

inline void write_u32_le(std::vector<uint8_t>& buf, uint32_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void write_bytes(std::vector<uint8_t>& buf, const uint8_t* data, std::size_t len) {
  buf.insert(buf.end(), data, data + len);
}

inline void write_null_str(std::vector<uint8_t>& buf, std::string_view s) {
  buf.insert(buf.end(), s.begin(), s.end());
  buf.push_back(0x00);
}

inline void write_lenenc_int(std::vector<uint8_t>& buf, uint64_t v) {
  if (v < 251) {
    buf.push_back(static_cast<uint8_t>(v));
  } else if (v < 65536) {
    buf.push_back(0xFC);
    write_u16_le(buf, static_cast<uint16_t>(v));
  } else if (v < 16777216) {
    buf.push_back(0xFD);
    write_u24_le(buf, static_cast<uint32_t>(v));
  } else {
    buf.push_back(0xFE);
    for (int i = 0; i < 8; ++i)
      buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

inline void write_lenenc_str(std::vector<uint8_t>& buf, std::string_view s) {
  write_lenenc_int(buf, s.size());
  buf.insert(buf.end(), s.begin(), s.end());
}

// ── Read helpers ──────────────────────────────────────────────────────────

inline uint16_t read_u16_le(const uint8_t* p) {
  return static_cast<uint16_t>(
      static_cast<uint16_t>(p[0]) |
      static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t read_u24_le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16);
}

inline uint32_t read_u32_le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Returns decoded value; advances *pos past the decoded bytes.
inline uint64_t read_lenenc_int(const uint8_t* data, std::size_t size,
                                 std::size_t* pos) {
  if (*pos >= size) return 0;
  uint8_t first = data[(*pos)++];
  if (first < 0xFB) return first;
  if (first == 0xFC) {
    if (*pos + 2 > size) return 0;
    auto v = read_u16_le(data + *pos);
    *pos += 2;
    return v;
  }
  if (first == 0xFD) {
    if (*pos + 3 > size) return 0;
    auto v = read_u24_le(data + *pos);
    *pos += 3;
    return v;
  }
  // 0xFE: 8-byte integer
  if (*pos + 8 > size) return 0;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<uint64_t>(data[(*pos)++]) << (8 * i);
  return v;
}

// Reads a null-terminated string; advances *pos past the null byte.
inline std::string read_null_str(const uint8_t* data, std::size_t size,
                                  std::size_t* pos) {
  std::string s;
  while (*pos < size && data[*pos] != 0x00) s += static_cast<char>(data[(*pos)++]);
  if (*pos < size) ++(*pos);  // skip null
  return s;
}

// Reads a length-encoded string; advances *pos.
inline std::string read_lenenc_str(const uint8_t* data, std::size_t size,
                                    std::size_t* pos) {
  auto len = read_lenenc_int(data, size, pos);
  if (*pos + len > size) return {};
  std::string s(reinterpret_cast<const char*>(data + *pos), len);
  *pos += len;
  return s;
}

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_CODEC_H_
