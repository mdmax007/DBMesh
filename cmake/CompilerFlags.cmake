# dbmesh_warnings — INTERFACE target applied to every dbmesh library and binary.
# Links against this target to inherit the project warning policy.

add_library(dbmesh_warnings INTERFACE)

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

# AddressSanitizer + UBSan in Debug builds
target_compile_options(dbmesh_warnings INTERFACE
  $<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-omit-frame-pointer>
)
target_link_options(dbmesh_warnings INTERFACE
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
