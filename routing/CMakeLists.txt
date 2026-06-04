add_library(dbmesh_routing INTERFACE)
target_include_directories(dbmesh_routing INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_routing INTERFACE
  dbmesh_core
)
