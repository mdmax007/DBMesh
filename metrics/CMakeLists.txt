add_library(dbmesh_metrics INTERFACE)
target_include_directories(dbmesh_metrics INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(dbmesh_metrics INTERFACE
  dbmesh_core
)

if(prometheus-cpp_FOUND)
  # Built with ENABLE_PULL=OFF — use core only; DBMesh serves /metrics via Boost.Beast
  target_link_libraries(dbmesh_metrics INTERFACE prometheus-cpp::core)
  target_compile_definitions(dbmesh_metrics INTERFACE DBMESH_PROMETHEUS_ENABLED)
endif()

if(opentelemetry-cpp_FOUND)
  target_link_libraries(dbmesh_metrics INTERFACE
    opentelemetry-cpp::trace
    opentelemetry-cpp::otlp_http_exporter
  )
  target_compile_definitions(dbmesh_metrics INTERFACE DBMESH_OTEL_ENABLED)
endif()
