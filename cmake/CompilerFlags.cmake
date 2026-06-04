# dbmesh_warnings — INTERFACE target applied to every dbmesh library and binary.
# Links against this target to inherit the project warning policy.

add_library(dbmesh_warnings INTERFACE)

# Boost 1.74 detects Clang coroutines via the old TS macro (__cpp_coroutines),
# not the C++20 standard macro (__cpp_impl_coroutine). Force the defines so
# boost::asio::awaitable is visible with any C++20 compiler.
target_compile_definitions(dbmesh_warnings INTERFACE
  BOOST_ASIO_HAS_CO_AWAIT=1
  BOOST_ASIO_HAS_STD_COROUTINE=1
)

target_compile_options(dbmesh_warnings INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wno-unused-parameter  # remove once all stubs are filled in
  -Werror
)

# AddressSanitizer + UBSan — separate INTERFACE target so coroutine-using
# modules (protocol/mysql) can opt out. Clang 14 assembler cannot represent
# ASAN section differences in C++20 coroutine frames; enable ASAN only when
# DBMESH_ASAN=ON (default ON for Clang 16+, OFF otherwise).
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
  option(DBMESH_ASAN "Enable AddressSanitizer" OFF)
else()
  option(DBMESH_ASAN "Enable AddressSanitizer" ON)
endif()

add_library(dbmesh_asan INTERFACE)
if(DBMESH_ASAN)
  target_compile_options(dbmesh_asan INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-omit-frame-pointer>
  )
  target_link_options(dbmesh_asan INTERFACE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
  )
endif()
