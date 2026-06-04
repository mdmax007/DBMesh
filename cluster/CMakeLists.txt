add_library(dbmesh_cluster INTERFACE)
target_include_directories(dbmesh_cluster INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_cluster INTERFACE
  dbmesh_core
  dbmesh_storage
)
