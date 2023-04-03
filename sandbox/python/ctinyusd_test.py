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
        "Scope",
        "Xform",
        "Mesh",
        "GeomSubset",
        "Camera",
        "Material",
        "Shader",
        "SphereLight",
        "DistantLight",
        "RectLight",
        # TODO: More Prim types...
        ]
    
    # TODO: better init constructor
    def __init__(self, prim_type:str = "Model", name:str = None, from_handle = None):

        if from_handle is not None:
            # Create a Python Prim instance with handle(No copies of C object)

            print("Create Prim from handle.")
            self._handle = from_handle 
            #assert self._handle 

            self._name = name
            self._prim_type = ctinyusd.c_tinyusd_prim_type(self._handle)

            self._is_handle_reference = True

        else:
            # New Prim with prim_type
            if prim_type not in self._builtin_types:
                raise RuntimeError("Unsupported/unimplemented Prim type: ", prim_type)

            self._prim_type = prim_type
            err = ctinyusd.c_tinyusd_string_new_empty()
            self._handle = ctinyusd.c_tinyusd_prim_new(prim_type, err)

            if self._handle is False:
                raise RuntimeError("Failed to new Prim:" + ctinyusd.c_tinyusd_string_str(err))

            ctinyusd.c_tinyusd_string_free(err)

            self._is_handle_reference = False

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
        # Return list
        # TODO: Consider use generator?(but len() is not available)

        child_list = []

        n = ctinyusd.c_tinyusd_prim_num_children(self._handle)
        for i in range(n):
            child_ptr = ctypes.POINTER(ctinyusd.CTinyUSDPrim)()

            ret = ctinyusd.c_tinyusd_prim_get_child(self._handle, i, ctypes.byref(child_ptr))
            assert ret

            child_prim = Prim(from_handle=child_ptr)

            child_list.append(child_prim)

        return child_list

    def add_child(self, child_prim):
        assert isinstance(child_prim, Prim)

        print("self:", self._handle)
        print("child:", child_prim._handle)
        ret = ctinyusd.c_tinyusd_prim_append_child(self._handle, child_prim._handle)
        print("append child:", ret)
        assert ret == 1

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
            child_ptr = ctypes.POINTER(ctinyusd.CTinyUSDPrim)
            ret = ctinyusd.c_tinyusd_prim_get_child(self._handle, ctypes.byref(child_ptr))
            assert ret == 1

            child_prim = Prim(from_handle = child_ptr)

        raise StopIterator



#t = Token("mytok")
#print(t)

#dt = copy.copy(t)
#del t
#print(dt)

root = Prim("Xform")
print("root", root.prim_type)

xform_prim = Prim("Xform")
print("child", xform_prim.prim_type)

root.add_child(xform_prim)
print("added child.")

print("# of child = ", len(root.children()))
for child in root.children():
    print(child)

#cp = Prim(from_handle=p._handle)
#print(cp)

#a = copy.deepcopy(p)

#for child in p.children():
#    print(child)

#print("del p")
#del p
