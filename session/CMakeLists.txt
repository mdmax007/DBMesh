add_library(dbmesh_session INTERFACE)
target_include_directories(dbmesh_session INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_session INTERFACE
  dbmesh_core
  dbmesh_routing
)
