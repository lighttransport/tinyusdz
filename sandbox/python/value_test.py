import ctypes
import struct

import ctinyusdz
import array

arr = array.array('i', [1, 2, 3, 4])
print(arr)

int1d = (ctypes.c_int * 10)()
print(int1d)

pvar = ctinyusdz.PrimVar()
pvar.set_array(arr)

gv = pvar.get_array()
# numpy.array, but we can use it without numpy module.
print(gv.size)
print(gv)

#ctinyiusdz.
#
