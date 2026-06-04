add_library(dbmesh_pool STATIC
  src/connection_pool.cpp
  src/pool_manager.cpp
  src/mysql_backend_connection.cpp
  src/backend_connector.cpp
)
target_include_directories(dbmesh_pool PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_pool PUBLIC
  dbmesh_core
  dbmesh_protocol_mysql
)
# Coroutine module — intentionally does NOT link dbmesh_asan (Clang 14 cannot
# sanitize C++20 coroutines). See cmake/CompilerFlags.cmake.

if(DBMESH_BUILD_TESTS)
  add_executable(test_dbmesh_pool
    src/connection_pool_test.cpp
    src/pool_manager_test.cpp
  )
  target_link_libraries(test_dbmesh_pool PRIVATE
    dbmesh_pool
    GTest::gtest_main
    GTest::gmock
  )
  gtest_discover_tests(test_dbmesh_pool)
endif()
