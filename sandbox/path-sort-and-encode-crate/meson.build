crate_encoding_lib = static_library('crate-encoding',
  'src/path_sort.cc',
  'src/tree_encode.cc',
  include_directories: include_directories('include'),
  pic: true,
)

crate_encoding_dep = declare_dependency(
  link_with: crate_encoding_lib,
  include_directories: include_directories('include'),
)
