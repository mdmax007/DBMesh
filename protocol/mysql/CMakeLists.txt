add_library(dbmesh_protocol_mysql INTERFACE)
target_include_directories(dbmesh_protocol_mysql INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_protocol_mysql INTERFACE
  dbmesh_core
  dbmesh_routing
  dbmesh_session
  dbmesh_pool
  dbmesh_security
  OpenSSL::SSL
  OpenSSL::Crypto
)
