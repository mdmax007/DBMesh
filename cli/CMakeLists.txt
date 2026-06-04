add_executable(dbmeshctl src/main.cpp)
target_link_libraries(dbmeshctl PRIVATE
  dbmesh_core
  dbmesh_warnings
  Boost::headers  # Boost.Beast for HTTP client
  Boost::system
  Threads::Threads
)
install(TARGETS dbmeshctl RUNTIME DESTINATION bin)
