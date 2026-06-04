cmake_minimum_required(VERSION 3.22)
project(dbmesh
  VERSION 0.1.0
  DESCRIPTION "Distributed Database Routing Mesh"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(DBMESH_BUILD_TESTS "Build tests" ON)

include(CompilerFlags)
include(Dependencies)

# enable_testing() must precede any add_subdirectory that calls gtest_discover_tests
if(DBMESH_BUILD_TESTS)
  enable_testing()
endif()

# ── Modules (INTERFACE libraries — implementations added per milestone) ──
add_subdirectory(core)
add_subdirectory(protocol)
add_subdirectory(routing)
add_subdirectory(session)
add_subdirectory(pool)
add_subdirectory(failover)
add_subdirectory(cluster)
add_subdirectory(security)
add_subdirectory(storage)
add_subdirectory(api)
add_subdirectory(metrics)

# ── Main binary ─────────────────────────────────────────────────────────
add_executable(dbmesh core/src/main.cpp)
target_link_libraries(dbmesh PRIVATE
  dbmesh_core
  dbmesh_protocol_mysql
  dbmesh_routing
  dbmesh_session
  dbmesh_pool
  dbmesh_failover
  dbmesh_cluster
  dbmesh_security
  dbmesh_storage
  dbmesh_api
  dbmesh_metrics
  dbmesh_warnings
  Threads::Threads
)
# dbmesh binary links dbmesh_asan directly so non-coroutine code is still
# covered by ASAN even though protocol/mysql opts out.
target_link_libraries(dbmesh PRIVATE dbmesh_asan)
install(TARGETS dbmesh RUNTIME DESTINATION bin)

# ── CLI tool ────────────────────────────────────────────────────────────
add_subdirectory(cli)

# ── Tests ───────────────────────────────────────────────────────────────
if(DBMESH_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ── clang-format target ─────────────────────────────────────────────────
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  set(_FMT_DIRS core protocol routing session pool failover cluster security
    storage api metrics cli tests)
  set(_FMT_SOURCES "")
  foreach(_dir ${_FMT_DIRS})
    file(GLOB_RECURSE _files
      "${CMAKE_SOURCE_DIR}/${_dir}/*.cpp"
      "${CMAKE_SOURCE_DIR}/${_dir}/*.h"
    )
    list(APPEND _FMT_SOURCES ${_files})
  endforeach()
  add_custom_target(clang-format
    COMMAND ${CLANG_FORMAT} -i ${_FMT_SOURCES}
    COMMENT "Formatting source files with clang-format"
    VERBATIM
  )
  add_custom_target(clang-format-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${_FMT_SOURCES}
    COMMENT "Checking source formatting with clang-format"
    VERBATIM
  )
endif()

# ── clang-tidy target ───────────────────────────────────────────────────
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
  add_custom_target(clang-tidy
    COMMAND ${CMAKE_SOURCE_DIR}/cmake/run_clang_tidy.sh
      ${CLANG_TIDY}
      ${CMAKE_BINARY_DIR}/compile_commands.json
      ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy"
    VERBATIM
  )
endif()
