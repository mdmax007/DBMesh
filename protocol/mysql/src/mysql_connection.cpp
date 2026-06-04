#include "dbmesh/protocol/mysql/mysql_connection.h"

#include "dbmesh/protocol/mysql/constants.h"
#include "dbmesh/protocol/mysql/handshake.h"
#include "dbmesh/protocol/mysql/packets.h"

#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace dbmesh::protocol::mysql {

namespace asio = boost::asio;

MySqlConnection::MySqlConnection(asio::ip::tcp::socket socket,
                                  uint32_t connection_id,
                                  std::shared_ptr<const Config> config)
    : socket_(std::move(socket)),
      connection_id_(connection_id),
      config_(std::move(config)),
      logger_(Logger::get("mysql.conn")) {}

asio::awaitable<void> MySqlConnection::start(
    std::shared_ptr<MySqlConnection> conn) {
  // Storing conn in the coroutine frame keeps the connection alive for the
  // entire duration of the handshake and command loop.
  co_await conn->run();
}

asio::awaitable<void> MySqlConnection::run() {
  auto self = shared_from_this();  // belt-and-suspenders
  try {
    co_await do_run();
  } catch (const boost::system::system_error& e) {
    if (e.code() != boost::asio::error::eof &&
        e.code() != boost::asio::error::connection_reset &&
        e.code() != boost::asio::error::broken_pipe) {
      logger_->warn("connection " + std::to_string(connection_id_) +
                    " error: " + e.what());
    }
  } catch (const std::exception& e) {
    logger_->error("connection " + std::to_string(connection_id_) +
                   " fatal: " + e.what());
  }
}

asio::awaitable<void> MySqlConnection::do_run() {
  // Apply socket options
  socket_.set_option(asio::ip::tcp::no_delay(true));

  // ── 1. Send HandshakeV10 ────────────────────────────────────────────
  HandshakeV10 hs;
  hs.connection_id   = connection_id_;
  hs.server_version  = config_->node.name.empty()
                           ? "8.0.30-dbmesh"
                           : "8.0.30-dbmesh-" + config_->node.name;
  hs.generate_auth_data();
  auto auth_data = hs.auth_data;

  seq_ = 0;
  co_await send(hs.encode());

  // ── 2. Receive HandshakeResponse41 ─────────────────────────────────
  seq_ = 1;
  auto pkt_result = co_await framer_.read_packet(socket_);
  if (is_err(pkt_result)) co_return;

  auto parse_result = HandshakeResponse41::decode(get_value(pkt_result).payload);
  if (is_err(parse_result)) {
    co_await send_err(err::ACCESS_DENIED, "28000", "Protocol error");
    co_return;
  }
  auto& resp = get_value(parse_result);
  client_caps_ = resp.capability_flags & caps::SERVER_FLAGS;
  username_    = resp.username;
  current_db_  = resp.database;

  // ── 3. Authenticate ─────────────────────────────────────────────────
  // M1.2 stub: accept any user. Real auth (bcrypt + users.yaml) in M1.9.
  // The wire protocol for mysql_native_password is fully implemented;
  // we just skip the actual user-lookup/password comparison.
  bool authenticated = true;  // TODO(M1.9): call AuthChain::verify()
  (void)auth_data;             // will be passed to verify() in M1.9

  if (!authenticated) {
    co_await send_err(err::ACCESS_DENIED, "28000",
                      "Access denied for user '" + username_ + "'");
    co_return;
  }

  logger_->debug("connection " + std::to_string(connection_id_) +
                 " authenticated as '" + username_ + "'");

  // ── 4. Send OK ──────────────────────────────────────────────────────
  seq_ = 2;
  co_await send_ok();

  // ── 5. Command loop ─────────────────────────────────────────────────
  co_await command_loop();
}

asio::awaitable<void> MySqlConnection::command_loop() {
  for (;;) {
    seq_ = 0;
    auto result = co_await framer_.read_packet(socket_);
    if (is_err(result)) co_return;

    const auto& pkt = get_value(result);
    if (pkt.payload.empty()) co_return;

    bool quit = co_await handle_command(pkt);
    if (quit) co_return;
  }
}

asio::awaitable<bool> MySqlConnection::handle_command(const MySqlPacket& pkt) {
  seq_ = pkt.sequence + 1;
  const uint8_t command = pkt.payload[0];

  switch (command) {
    case cmd::QUIT:
      co_return true;

    case cmd::PING:
      co_await send_ok();
      co_return false;

    case cmd::INIT_DB:
      co_await handle_com_init_db(pkt.payload);
      co_return false;

    case cmd::QUERY:
      co_await handle_com_query(pkt.payload);
      co_return false;

    case cmd::FIELD_LIST:
      co_await handle_com_field_list(pkt.payload);
      co_return false;

    case cmd::STATISTICS: {
      std::string stats = "DBMesh connection_id=" + std::to_string(connection_id_);
      co_await send(std::vector<uint8_t>(stats.begin(), stats.end()));
      co_return false;
    }

    case cmd::SET_OPTION:
      co_await send_ok();
      co_return false;

    case cmd::STMT_PREPARE:
    case cmd::STMT_EXECUTE:
    case cmd::STMT_CLOSE:
    case cmd::STMT_RESET:
      co_await send_err(err::UNKNOWN_COM_ERROR, "08S01",
                        "Binary protocol not yet supported (Milestone 1.19)");
      co_return false;

    default:
      co_await send_err(err::UNKNOWN_COM_ERROR, "08S01",
                        "Unknown command " + std::to_string(command));
      co_return false;
  }
}

// ── COM_INIT_DB ───────────────────────────────────────────────────────────

asio::awaitable<void> MySqlConnection::handle_com_init_db(
    const std::vector<uint8_t>& payload) {
  if (payload.size() > 1)
    current_db_.assign(payload.begin() + 1, payload.end());
  co_await send_ok();
}

// ── COM_FIELD_LIST ────────────────────────────────────────────────────────

asio::awaitable<void> MySqlConnection::handle_com_field_list(
    const std::vector<uint8_t>& /*payload*/) {
  // Return empty field list + EOF
  co_await send(make_eof());
}

// ── COM_QUERY ─────────────────────────────────────────────────────────────

namespace {

std::string normalise(const std::string& sql) {
  std::string out;
  out.reserve(sql.size());
  bool in_space = true;
  for (char c : sql) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space) out += ' ';
      in_space = true;
    } else {
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      in_space = false;
    }
  }
  return out;
}

bool starts_with(const std::string& s, const char* p) {
  return s.rfind(p, 0) == 0;
}

// Builds all packet payloads for a query stub response.
// Returns empty vector for SET/USE (caller sends OK).
// Returns {special="use:<db>"} for USE statements.
// GCC 11.4 ICE workaround: all complex logic lives here, outside the coroutine.
std::vector<std::vector<uint8_t>> build_stub_response(
    const std::string& norm,
    const std::string& current_db) {
  using namespace dbmesh::protocol::mysql;

  // Classic text result set (no DEPRECATE_EOF):
  //   column_count | column_def... | EOF | row... | EOF
  if (norm == "SELECT 1" || norm == "SELECT 1 ;") {
    return {make_column_count(1),
            make_column_def("1", col_type::VAR_STRING, 1),
            make_eof(),
            make_text_row({"1"}),
            make_eof()};
  }
  if (starts_with(norm, "SELECT DATABASE()")) {
    return {make_column_count(1),
            make_column_def("DATABASE()", col_type::VAR_STRING, 64),
            make_eof(),
            make_text_row({current_db}),
            make_eof()};
  }
  if (starts_with(norm, "SELECT VERSION()") ||
      starts_with(norm, "SELECT @@VERSION") ||
      starts_with(norm, "SELECT @@version")) {
    return {make_column_count(1),
            make_column_def("VERSION()", col_type::VAR_STRING, 64),
            make_eof(),
            make_text_row({"8.0.30-dbmesh"}),
            make_eof()};
  }
  if (starts_with(norm, "SELECT @@") ||
      starts_with(norm, "SHOW ") ||
      starts_with(norm, "SELECT SLEEP(")) {
    return {make_column_count(1),
            make_column_def("Value", col_type::VAR_STRING, 255),
            make_eof(),
            make_eof()};  // no rows
  }
  return {};  // caller decides (SET/USE/error)
}

} // namespace

asio::awaitable<void> MySqlConnection::handle_com_query(
    const std::vector<uint8_t>& payload) {
  std::string sql(payload.begin() + 1, payload.end());
  std::string norm = normalise(sql);

  logger_->debug("conn " + std::to_string(connection_id_) +
                 " query: " + sql.substr(0, 60));

  if (starts_with(norm, "SET ")) {
    co_await send_ok();
    co_return;
  }

  if (starts_with(norm, "USE ")) {
    std::string db = sql.substr(4);
    while (!db.empty() &&
           std::isspace(static_cast<unsigned char>(db.back())))
      db.pop_back();
    current_db_ = db;
    co_await send_ok();
    co_return;
  }

  auto packets = build_stub_response(norm, current_db_);
  if (!packets.empty()) {
    for (auto& pkt : packets) co_await send(pkt);
    co_return;
  }

  // M1.5+: route to RoutingEngine. Stub error for now.
  std::string msg = "DBMesh: routing not yet implemented (M1.5). SQL: " +
                    sql.substr(0, 80);
  co_await send_err(1105, "HY000", msg);
}

// ── Wire helpers ──────────────────────────────────────────────────────────

asio::awaitable<void> MySqlConnection::send(const std::vector<uint8_t>& payload) {
  auto r = co_await framer_.write_packet(socket_, payload, seq_);
  if (is_err(r))
    throw boost::system::system_error(boost::asio::error::broken_pipe);
}

asio::awaitable<void> MySqlConnection::send_ok(uint64_t affected,
                                                uint64_t insert_id,
                                                uint16_t status_flags) {
  co_await send(make_ok(affected, insert_id, status_flags));
}

asio::awaitable<void> MySqlConnection::send_err(uint16_t code,
                                                  std::string_view sql_state,
                                                  std::string_view msg) {
  co_await send(make_err(code, sql_state, msg));
}

} // namespace dbmesh::protocol::mysql
