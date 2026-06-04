add_library(dbmesh_pool INTERFACE)
target_include_directories(dbmesh_pool INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_pool INTERFACE
  dbmesh_core
)
