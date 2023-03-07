from typing import Union

"""
To distinguish pathlib.Path, Use USD prefix
"""
class USDPath:
    def __init__(self, prim_part: str = None, prop_part: str = None):
        self.prim_part = prim_part
        self.prop_part = prop_part

    def is_root_path(self):
        if self.prop_part is not None:
            return False

        # Empty str = root.
        if self.prim_part == "":
            return True

    def is_prop_path(self):
        """Test if a path is Prim property path
        """
        if is_root_path():
            return False

        # At the moment, Prim part must exist
        if self.prim_part is None:
            return False

        if self.prop_part is None:
            return False

        return True

    def is_prim_path(self):
        if self.prop_part is not None:
            return False

        if self.prim_part is None:
            return True

        return False;

    def __str__(self):
        if self.is_root_path():
            return "/"

        if self.is_prim_path():
            return self.primp_part

        i

class TimeSampleData:
    def __init__(self, t: float, value):
        self.blocked: bool = False # Value block
        self.t: float = t
        self.value = value;
        
    def set_blocked(self):
        self.blocked = True

    def is_blocked(self):
        return self.blocked


class TimeSamples:
    def __init__(self, dtype: str):
        self.ts: dict = {}

    def set(self, t: float, value):
        self.ts[t] = TimeSampleData(t, value)

    def get(self, t: float):
        # TODO: interpolate time.
        return self.ts[t]

    def keys(self):
        self.ts.keys()

    def values(self):
        self.ts.values()


class Prim:

    # TODO: support Union 
    _builtin_props = {
        'name': (str, ""),
        'prim_type': (str, ""),
        'spec': (str, "def"),
        'element_name': (str, "")
    }

    _builtin_metadata_props = {
        'active': (bool, None),
    }

    def __init__(self, name: str = None, prim_type: Union[str, None] = None):

        # builtin props
        for (k, (ty, val)) in self._builtin_props.items():
            self.__dict__[k] = val

        # metadatum
        self.__dict__["_metas"] = {}
        for (k, (ty, val)) in self._builtin_metadata_props.items():
            self.__dict__["_metas"][k] = val

        # custom props
        self.__dict__["_props"] = {}


    def __setattr__(self, name, value):
        if name in self._builtin_props:
            (ty, _) = self._builtin_props[name]
            if not isinstance(value, ty):
                raise TypeError("Built-in property `{}` is type of `{}`, but got type `{}` for the value.".format(name, ty.__name__, type(value).__name__)) 

        print("setattr", name, value)
        self.__dict__["_props"][name] = value
        #self._props[name] = value

    def __getattr__(self, name):
        if name in self._builtin_props:
            print("builtin")
            return self.__dict__[name]

        print("getattr", name)
        return self.__dict__["_props"][name]

    def custom_props(self):
        return self.__dict__["_props"]

    def metas(self):
        return self.__dict__["_metas"]

    def __str__(self):
        s = "{} ".format(self.spec)
        if self.prim_type is not None:
            s += "{} ".format(self.prim_type)
        s += self.name
        s += " (\n"
        for (k, v) in self.metas().items():
            if v is not None:
                s += "  {} = {}\n".format(k, v)
        s += ") { \n"

        ## print props
        for (k, v) in self.custom_props().items():
            s += "  " + str(k) + " " + str(v) + "\n"

        s += "}\n"

        return s

    def __repr__(self):

        return self.__str__()
  

prim = Prim()
prim.prim_type = "aa"
prim.points = [1, 2, 3]

prim.metas()["active"] = False
#prim.name = "aa"
#prim.bora = "ss"
print(prim)
