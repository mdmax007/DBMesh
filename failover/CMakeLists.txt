add_library(dbmesh_failover INTERFACE)
target_include_directories(dbmesh_failover INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_failover INTERFACE
  dbmesh_core
  dbmesh_pool
  dbmesh_session
)
