add_library(dbmesh_core STATIC
  src/config.cpp
  src/logger.cpp
  src/application.cpp
)
target_include_directories(dbmesh_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_core PUBLIC
  dbmesh_warnings
  Boost::headers
  Boost::system
  yaml-cpp
  Threads::Threads
)
# dbmesh_asan is PRIVATE so coroutine modules (dbmesh_protocol_mysql) that
# depend on dbmesh_core do NOT inherit ASAN flags from us.
target_link_libraries(dbmesh_core PRIVATE dbmesh_asan)
# application.cpp wires the frontend, pool, and routing engine. We add their
# include paths here without a link-time circular dep; the dbmesh binary links
# the actual libraries (symbols resolve at the final executable link).
target_include_directories(dbmesh_core PRIVATE
  ${CMAKE_SOURCE_DIR}/protocol/mysql/include
  ${CMAKE_SOURCE_DIR}/pool/include
  ${CMAKE_SOURCE_DIR}/routing/include
)

if(DBMESH_BUILD_TESTS)
  add_executable(test_dbmesh_core
    src/types_test.cpp
    src/config_test.cpp
    src/logger_test.cpp
  )
  target_link_libraries(test_dbmesh_core PRIVATE
    dbmesh_core
    GTest::gtest_main
    GTest::gmock
  )
  gtest_discover_tests(test_dbmesh_core)
endif()
