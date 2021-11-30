project('fuzz_tinyusdz', 'c', 'cpp', default_options : ['cpp_std=c++14'])

tinyusdz_sources=[
  '../../src/pxrLZ4/lz4.cpp',
  '../../src/tinyusdz.cc',
  '../../src/lz4-compression.cc',
  '../../src/integerCoding.cpp',
  '../../src/io-util.cc',
  '../../src/pprinter.cc',
  '../../src/prim-types.cc',
  '../../src/usdObj.cc',
  # deps
  '../../src/external/ryu/ryu/d2s.c',
  '../../src/external/ryu/ryu/f2s.c',
  '../../src/external/ryu/ryu/s2f.c',
  '../../src/external/ryu/ryu/s2d.c'
  ]

incdirs = include_directories(['../../src','../../src/external/ryu'])
executable('fuzz_tinyusdz',
  'tinyusdz_fuzzmain.cc',
  tinyusdz_sources, 
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


executable('fuzz_usdaparser',
  'usdaparser_fuzzmain.cc',
  tinyusdz_sources, 
  '../../src/usda-parser.cc',
  include_directories : incdirs,
  cpp_args : '-fsanitize=address,fuzzer',
  link_args : '-fsanitize=address,fuzzer' )
