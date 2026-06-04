#include "dbmesh/core/application.h"
#include "dbmesh/protocol/mysql/mysql_frontend.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

namespace dbmesh {

Application::Application(std::string config_path)
    : config_path_(std::move(config_path)),
      signals_(io_context_) {
  auto result = ConfigLoader::load(config_path_);
  if (is_err(result))
    throw std::runtime_error("failed to load config: " + get_error(result));

  config_ = std::make_shared<const Config>(std::move(get_value(result)));

  Logger::init(config_->logging);
  logger_ = Logger::get("application");

  logger_->info("DBMesh v0.1.0 starting");
  logger_->info("config loaded from " + config_path_);
}

Application::~Application() {
  remove_pid_file();
}

int Application::run() {
  write_pid_file();
  arm_signals();

  try {
    start_subsystems();
  } catch (const std::exception& e) {
    logger_->fatal(std::string("startup failed: ") + e.what());
    Logger::shutdown();
    return EXIT_FAILURE;
  }

  logger_->info("accepting connections");

  // Spawn (io_threads - 1) extra threads; the main thread also runs the loop.
  uint32_t extra = (config_->listeners.mysql.io_threads > 1)
                       ? config_->listeners.mysql.io_threads - 1
                       : 0;
  io_threads_.reserve(extra);
  for (uint32_t i = 0; i < extra; ++i) {
    io_threads_.emplace_back([this] { io_context_.run(); });
  }

  io_context_.run();  // blocks until work_guard is released

  for (auto& t : io_threads_) t.join();
  io_threads_.clear();

  stop_subsystems();
  Logger::shutdown();
  return EXIT_SUCCESS;
}

void Application::request_shutdown() {
  if (shutting_down_.exchange(true)) return;
  logger_->info("shutdown requested");
  // Post to the io_context so stop() runs inside the event loop.
  boost::asio::post(io_context_, [this] { io_context_.stop(); });
}

void Application::arm_signals() {
  signals_.add(SIGTERM);
  signals_.add(SIGINT);
  signals_.add(SIGHUP);

  signals_.async_wait([this](const boost::system::error_code& ec, int signo) {
    if (!ec) on_signal(signo);
  });
}

void Application::on_signal(int signo) {
  if (signo == SIGTERM || signo == SIGINT) {
    request_shutdown();
    return;
  }

  if (signo == SIGHUP) {
    logger_->info("SIGHUP received — reloading config");
    ConfigReloader reloader{config_path_,
                            [this](std::shared_ptr<const Config> c) {
                              on_config_reload(std::move(c));
                            }};
    reloader.reload();
    // Re-arm for next signal only when not shutting down.
    if (!shutting_down_) arm_signals();
    return;
  }

  if (!shutting_down_) arm_signals();
}

void Application::on_config_reload(std::shared_ptr<const Config> new_config) {
  config_ = std::move(new_config);
  logger_->info("config reloaded successfully");
  // Milestone 1.17+: emit ConfigReloadedEvent to subsystems.
}

void Application::start_subsystems() {
  if (config_->listeners.mysql.enabled) {
    mysql_frontend_ = std::make_unique<protocol::mysql::MySqlFrontend>(
        io_context_, config_);
    mysql_frontend_->start();
    logger_->info("MySQL frontend listening on port " +
                  std::to_string(config_->listeners.mysql.port));
  }
  // Milestone 1.3+: init pool manager
  // Milestone 1.7+: start health monitor
  // Milestone 1.12+: start HTTP API server
}

void Application::stop_subsystems() {
  if (mysql_frontend_) {
    mysql_frontend_->stop();
    mysql_frontend_.reset();
  }
  logger_->info("all subsystems stopped");
}

void Application::write_pid_file() {
  pid_file_path_ = config_->node.data_dir + "/dbmesh.pid";
  std::error_code ec;
  std::filesystem::create_directories(config_->node.data_dir, ec);
  if (ec) {
    logger_->warn("cannot create data_dir '" + config_->node.data_dir +
                  "': " + ec.message() + " — skipping PID file");
    pid_file_path_.clear();
    return;
  }
  std::ofstream f(pid_file_path_);
  if (!f) logger_->warn("cannot write PID file: " + pid_file_path_);
  else f << static_cast<long>(::getpid()) << '\n';
}

void Application::remove_pid_file() {
  if (!pid_file_path_.empty())
    std::filesystem::remove(pid_file_path_);
}

} // namespace dbmesh
