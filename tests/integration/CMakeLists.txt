# Integration tests run against real MySQL/PostgreSQL backends.
# Start the test databases before running:
#   docker-compose -f tests/docker-compose.yml up -d
#
# Tests are tagged [integration] and excluded from the default unit test run.
#
# Pattern for each integration test:
#
#   add_executable(test_routing_integration routing_integration_test.cpp)
#   target_link_libraries(test_routing_integration
#     PRIVATE dbmesh_routing dbmesh_pool GTest::gtest_main
#   )
#   target_compile_definitions(test_routing_integration
#     PRIVATE DBMESH_INTEGRATION_TEST
#   )
#   gtest_discover_tests(test_routing_integration
#     PROPERTIES LABELS integration
#   )
