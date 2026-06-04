#ifndef DBMESH_PROTOCOL_MYSQL_TYPES_H_
#define DBMESH_PROTOCOL_MYSQL_TYPES_H_

#include <cstdint>
#include <vector>

namespace dbmesh::protocol::mysql {

struct MySqlPacket {
  uint32_t             length;
  uint8_t              sequence;
  std::vector<uint8_t> payload;
};

enum class IoError : uint8_t {
  READ_FAILED,
  WRITE_FAILED,
  PACKET_TOO_LARGE,
  CONNECTION_CLOSED,
};

} // namespace dbmesh::protocol::mysql

#endif // DBMESH_PROTOCOL_MYSQL_TYPES_H_
