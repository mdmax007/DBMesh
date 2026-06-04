add_library(dbmesh_api INTERFACE)
target_include_directories(dbmesh_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_api INTERFACE
  dbmesh_core
  dbmesh_routing
  dbmesh_session
  dbmesh_pool
  dbmesh_failover
  dbmesh_cluster
  dbmesh_security
  dbmesh_storage
  dbmesh_metrics
  Boost::headers  # Boost.Beast for HTTP server
)
