add_library(dbmesh_routing STATIC
  src/query_classifier.cpp
  src/schema_extractor.cpp
  src/read_write_router.cpp
)
target_include_directories(dbmesh_routing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_routing PUBLIC
  dbmesh_core
)
# Synchronous (non-coroutine) module — safe to sanitize under Clang 14.
target_link_libraries(dbmesh_routing PRIVATE dbmesh_asan)

if(DBMESH_BUILD_TESTS)
  add_executable(test_dbmesh_routing
    src/query_classifier_test.cpp
    src/schema_extractor_test.cpp
    src/read_write_router_test.cpp
  )
  target_link_libraries(test_dbmesh_routing PRIVATE
    dbmesh_routing
    dbmesh_asan
    GTest::gtest_main
    GTest::gmock
  )
  gtest_discover_tests(test_dbmesh_routing)
endif()
