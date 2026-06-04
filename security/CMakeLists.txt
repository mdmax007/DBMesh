add_library(dbmesh_security INTERFACE)
target_include_directories(dbmesh_security INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_security INTERFACE
  dbmesh_core
  dbmesh_storage
  OpenSSL::SSL
  OpenSSL::Crypto
)
