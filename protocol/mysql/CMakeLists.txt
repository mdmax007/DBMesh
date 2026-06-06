add_library(dbmesh_protocol_mysql STATIC
  src/packet_framer.cpp
  src/handshake.cpp
  src/mysql_connection.cpp
  src/mysql_frontend.cpp
  src/mysql_backend_connection.cpp
  src/backend_connector.cpp
)
target_include_directories(dbmesh_protocol_mysql PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_protocol_mysql PUBLIC
  dbmesh_core
  dbmesh_pool
  dbmesh_routing
  OpenSSL::SSL
  OpenSSL::Crypto
)

# Note: dbmesh_asan is intentionally NOT linked here.
# Clang 14 cannot compile C++20 coroutines with ASAN (assembler limitation).
# Re-enable by adding dbmesh_asan once Clang 16+ is the build compiler.

if(DBMESH_BUILD_TESTS)
  add_executable(test_dbmesh_protocol_mysql
    src/packet_framer_test.cpp
    src/handshake_test.cpp
  )
  target_link_libraries(test_dbmesh_protocol_mysql PRIVATE
    dbmesh_protocol_mysql
    GTest::gtest_main
    GTest::gmock
    OpenSSL::SSL
    OpenSSL::Crypto
  )
  gtest_discover_tests(test_dbmesh_protocol_mysql)
endif()
