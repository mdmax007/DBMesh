#include "dbmesh/core/application.h"
#include "dbmesh/core/config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [options]\n"
            << "  --config <path>   config file (default: dbmesh.yaml)\n"
            << "  --check-config    validate config and exit\n"
            << "  --version         print version and exit\n";
}

} // namespace

int main(int argc, char* argv[]) {
  std::string config_path  = "dbmesh.yaml";
  bool        check_config = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--check-config") {
      check_config = true;
    } else if (arg == "--version" || arg == "-v") {
      std::cout << "dbmesh 0.1.0\n";
      return EXIT_SUCCESS;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return EXIT_SUCCESS;
    } else {
      std::cerr << "unknown option: " << arg << '\n';
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (check_config) {
    auto result = dbmesh::ConfigLoader::load(config_path);
    if (dbmesh::is_err(result)) {
      std::cerr << "config error: " << dbmesh::get_error(result) << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "config OK\n";
    return EXIT_SUCCESS;
  }

  try {
    dbmesh::Application app(config_path);
    return app.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
