add_library(dbmesh_storage INTERFACE)
target_include_directories(dbmesh_storage INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_storage INTERFACE
  dbmesh_core
  SQLite3::SQLite3
)
