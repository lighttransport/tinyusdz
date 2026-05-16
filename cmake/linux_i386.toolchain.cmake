set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "i386")
set(CMAKE_C_COMPILER_TARGET i386-linux-gnu)

# Assume debian/ubuntu
#set(CMAKE_FIND_ROOT_PATH /usr/lib/i386-linux-gnu/)

# -m32 alone leaves gcc using x87 80-bit FP by default. That breaks
# strict float equality (e.g. `1.38f == 1.38f` after a parse-and-store
# round trip): intermediate values get held at 80-bit precision then
# rounded inconsistently against a 32-bit-stored float, producing
# false-negative comparisons. -msse2 -mfpmath=sse forces SSE2 single/
# double FP everywhere, which matches every other platform's behavior.
# All modern x86 hosts support SSE2 (mandatory since i686 baseline ~2003).
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -msse2 -mfpmath=sse")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -msse2 -mfpmath=sse")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# https://stackoverflow.com/questions/41557927/using-usr-lib-i386-linux-gnu-instead-of-usr-lib-x86-64-linux-gnu-to-find-libra
set(FIND_LIBRARY_USE_LIB64_PATHS OFF)
