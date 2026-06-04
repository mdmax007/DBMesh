# External dependencies for the DBMesh project. Requires CMake 3.22+.
#
# Required system packages (Ubuntu/Debian):
#   sudo apt-get install -y \
#     libboost-all-dev libyaml-cpp-dev libsqlite3-dev libssl-dev
#
# Required system packages (macOS):
#   brew install boost yaml-cpp sqlite openssl

# ── Threads ─────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ── Boost ───────────────────────────────────────────────────────────────
# Provides: Boost::headers (Asio, Beast, UUID — all header-only)
#           Boost::system (compiled library)
# Minimum 1.74 (Ubuntu 22.04 default). Boost.JSON requires 1.75+;
# install a newer Boost via PPA or from source for full JSON support.
find_package(Boost 1.74 REQUIRED COMPONENTS system)
# Boost::headers is available since Boost 1.70 CMake integration.
# Older Boost may only export Boost::boost — create an alias if needed.
if(NOT TARGET Boost::headers)
  add_library(Boost::headers ALIAS Boost::boost)
endif()

# ── yaml-cpp ────────────────────────────────────────────────────────────
find_package(yaml-cpp REQUIRED)

# ── SQLite3 ─────────────────────────────────────────────────────────────
find_package(SQLite3 REQUIRED)
# CMake <3.26 may find SQLite3 headers/libs but not register the imported target.
if(NOT TARGET SQLite3::SQLite3)
  add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
  set_target_properties(SQLite3::SQLite3 PROPERTIES
    IMPORTED_LOCATION             "${SQLite3_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}"
  )
endif()

# ── OpenSSL ─────────────────────────────────────────────────────────────
# Needed for TLS (frontend + backend) and bcrypt-adjacent hashing.
find_package(OpenSSL REQUIRED)

# ── prometheus-cpp (optional — Milestone 1.14) ──────────────────────────
find_package(prometheus-cpp QUIET)
if(prometheus-cpp_FOUND)
  message(STATUS "prometheus-cpp found — metrics enabled")
else()
  message(STATUS "prometheus-cpp not found — metrics stub only")
endif()

# ── GoogleTest (auto-fetched; available to all module test targets) ──────
if(DBMESH_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)
endif()

# ── OpenTelemetry C++ SDK (optional — Milestone 1.14) ───────────────────
# Find OTel's transitive deps first so their imported targets exist when
# opentelemetry-cpp's CMake config references them.
find_package(Protobuf QUIET)
find_package(CURL QUIET)
find_package(opentelemetry-cpp QUIET)
if(opentelemetry-cpp_FOUND)
  message(STATUS "opentelemetry-cpp found — tracing enabled")
else()
  message(STATUS "opentelemetry-cpp not found — tracing stub only")
endif()
