import copy
import ctypes
import ctinyusd

class Token(object):
    def __init__(self, tok):
        self._handle = ctinyusd.c_tinyusd_token_init()
        ret = ctinyusd.c_tinyusd_token_new(self._handle, tok)
        print("init", ret)

    def __copy__(self):
        print("copy")
        return Token(str(self))

    def __deepcopy__(self, memo):
        print("deepcopy", memo)
        return self.__copy__()

    def __del__(self):
        ret = ctinyusd.c_tinyusd_token_free(self._handle)
        print("del", ret)

    def __str__(self):
        btok = ctinyusd.c_tinyusd_token_str(self._handle)
        print(btok)
        return btok.decode()


class Prim(object):
    def __init__(self):
        self._handle = ctinyusd.CTinyUSDPrim()
        # FIXME
        ret = ctinyusd.c_tinyusd_prim_new("Xform", self._handle) 
        print("init", ret)

    def __copy__(self):
        print("copy")
        return Prim()

    def __deepcopy__(self, memo):
        print("deepcopy", memo)
        return self.__copy__()

    def __del__(self):
        ret = ctinyusd.c_tinyusd_prim_free(self._handle)
        print("del", ret)

t = Token("mytok")
print(t)

dt = copy.copy(t)
del t
print(dt)


p = Prim()

a = copy.deepcopy(p)

print("del a")
del a

print("del p")
del p
