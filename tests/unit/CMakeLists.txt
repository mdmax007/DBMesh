# Unit test executables are added here as components are implemented.
#
# Pattern for each component:
#
#   add_executable(test_query_classifier
#     ../../routing/src/QueryClassifier_test.cpp
#   )
#   target_link_libraries(test_query_classifier
#     PRIVATE dbmesh_routing GTest::gtest_main GTest::gmock
#   )
#   gtest_discover_tests(test_query_classifier)
