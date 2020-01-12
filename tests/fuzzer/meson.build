project('fuzz_tinyusdz', 'cpp', default_options : ['cpp_std=c++11'])

incdirs = include_directories('../../src')
executable('fuzz_tinyusdz',
  '../../src/pxrLZ4/lz4.cpp',
  'tinyusdz_fuzzmain.cc',
  '../../src/tinyusdz.cc',
  '../../src/lz4-compression.cc',
  '../../src/integerCoding.cpp',
  include_directories : incdirs,
  cpp_args : '-fsanitize=address,fuzzer',
  link_args : '-fsanitize=address,fuzzer' )

executable('fuzz_intcoding',
  '../../src/pxrLZ4/lz4.cpp',
  'intCoding_fuzzmain.cc',
  '../../src/lz4-compression.cc',
  '../../src/integerCoding.cpp',
  include_directories : incdirs,
  cpp_args : '-fsanitize=address,fuzzer',
  link_args : '-fsanitize=address,fuzzer' )
