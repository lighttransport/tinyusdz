import copy
import ctypes
import ctinyusd

class Token(object):

    def __init__(self, tok):
        self._handle = ctinyusd.c_tinyusd_token_new(tok)
        print("init", self._handle)

    def __copy__(self):
        print("copy(duplicate instance)")
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
    
    _builtin_types:str = [
        "Model", # Generic Prim type
        "Xform",
        "Mesh",
        # TODO..
        ]
    
    # TODO: better init construction
    def __init__(self, prim_type:str = "Model", from_handle = None):


        if from_handle is not None:
            # Create a Python Prim instance with handle(No copies in C layer)

            print("Create Prim from handle.")
            self._handle = from_handle 
            self._prim_type = ctinyusd.c_tinyusd_prim_type(self._handle)
            self._is_handle_reference = True

        else:
            # New Prim with prim_type
            if prim_type not in self._builtin_types:
                raise RuntimeError("Unsupported/unimplemented Prim type: ", prim_type)

            self._prim_type = prim_type
            self._handle = ctinyusd.c_tinyusd_prim_new(prim_type) 
            self._is_handle_reference = False
            print("init", self._handle)

    def __copy__(self):
        raise RuntimeError("Copying Prim in Python side is not supported at the moment.")

    def __deepcopy__(self, memo):
        print("deep copy")
        raise RuntimeError("Deep copying Prim in Python side is not supported at the moment.")

    def __del__(self):
        if not self._is_handle_reference:
            ret = ctinyusd.c_tinyusd_prim_free(self._handle)
            print("del", ret)

    def __getattr__(self, name):
        if name == 'prim_type':
            return self._prim_type   
        else:
            raise RuntimeError("Unknown Python attribute name:", name)
            
    def children(self):
        return PrimChildIterator(self._handle)

class PrimChildIterator:
    def __init__(self, handle):
        self._handle = handle
        self._num_children = ctinyusd.c_tinyusd_prim_num_children(self._handle)
        self._current_index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self._current_index < self._num_children:
            # Just a reference.
            child_handle = ctinyusd.c_tinyusd_prim_get_child(self._handle, self._current_index)
            child_prim = Prim(from_handle = child_handle)

            self._current_index += 1

            return child_prim

        raise StopIterator



#t = Token("mytok")
#print(t)

#dt = copy.copy(t)
#del t
#print(dt)

p = Prim("Xform")
print(p.prim_type)


cp = Prim(from_handle=p._handle)
print(cp)

#a = copy.deepcopy(p)

children = p.children()
print(children)

#print("del p")
#del p
