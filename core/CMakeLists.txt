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
