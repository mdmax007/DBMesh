#ifndef DBMESH_CORE_APPLICATION_H_
#define DBMESH_CORE_APPLICATION_H_

#include "dbmesh/core/config.h"
#include "dbmesh/core/logger.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dbmesh {

// Application owns every subsystem and defines the startup / shutdown order.
// Construction loads and validates config and initialises the logger.
// run() blocks until SIGTERM or SIGINT, then shuts down cleanly.
class Application {
 public:
  explicit Application(std::string config_path);
  ~Application();

  // Starts all subsystems then blocks until shutdown.
  // Returns EXIT_SUCCESS or EXIT_FAILURE.
  int run();

  // Thread-safe. May be called from any context (signal handler, API).
  void request_shutdown();

 private:
  void start_subsystems();
  void stop_subsystems();
  void arm_signals();
  void on_signal(int signo);
  void on_config_reload(std::shared_ptr<const Config> new_config);
  void write_pid_file();
  void remove_pid_file();

  std::string                       config_path_;
  std::shared_ptr<const Config>     config_;
  std::shared_ptr<Logger>           logger_;

  boost::asio::io_context           io_context_;
  boost::asio::signal_set           signals_;
  std::vector<std::thread>          io_threads_;

  std::atomic<bool>                 shutting_down_{false};
  std::string                       pid_file_path_;
};

} // namespace dbmesh

#endif // DBMESH_CORE_APPLICATION_H_
