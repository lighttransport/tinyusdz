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
  '../../src/primvar.cc',
  '../../src/ascii-parser.cc',
  '../../src/usda-reader.cc',
  '../../src/usda-writer.cc',
  '../../src/usdc-reader.cc',
  '../../src/usdc-writer.cc',
  '../../src/crate-reader.cc',
  '../../src/crate-format.cc',
  '../../src/crate-pprint.cc',
  '../../src/value-type.cc',
  '../../src/value-pprint.cc',
  # deps
  '../../src/external/fpng.cpp',
  '../../src/external/staticstruct.cc',
  '../../src/external/tinyxml2/tinyxml2.cpp',
  '../../src/external/string_id/string_id.cpp',
  '../../src/external/string_id/error.cpp',
  '../../src/external/string_id/database.cpp',
  #'../../src/external/ryu/ryu/d2s.c',
  #'../../src/external/ryu/ryu/f2s.c',
  #'../../src/external/ryu/ryu/s2f.c',
  #'../../src/external/ryu/ryu/s2d.c'
  ]

#incdirs = include_directories(['../../src','../../src/external/ryu'])
incdirs = include_directories(['../../src','../../src/external/fast_float/include'])

executable('fuzz_tinyusdz',
  'tinyusdz_fuzzmain.cc',
  tinyusdz_sources, 
  install: true,
  include_directories : incdirs,
  cpp_args : '-fsanitize=address,fuzzer',
  link_args : '-fsanitize=address,fuzzer' )

executable('fuzz_intcoding',
  '../../src/pxrLZ4/lz4.cpp',
  'intCoding_fuzzmain.cc',
  '../../src/lz4-compression.cc',
  '../../src/integerCoding.cpp',
  install: true,
  include_directories : incdirs,
  cpp_args : '-fsanitize=address,fuzzer',
  link_args : '-fsanitize=address,fuzzer' )


executable('fuzz_usdaparser',
  'usdaparser_fuzzmain.cc',
  tinyusdz_sources, 
  install: true,
  include_directories : incdirs,
  cpp_args : '-fsanitize=address,fuzzer',
  link_args : '-fsanitize=address,fuzzer' )


