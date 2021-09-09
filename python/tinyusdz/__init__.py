#
# Simple python classes to mimic pxr python binding.
#
from enum import Enum # from python3.4
import pytinyusd # native module

class Sdf:
    def __init__(self):
        pass


    class ValueTypeNames(Enum):
        Bool = 1
        Int = 2

    class Layer:
        def __init__(self):
            pass

        @classmethod
        def OpenAsAnonymous(cls, filepath: str, metadataOnly: False):
            layer = cls()
            print("TODO: OpenAsAnonymous")
            return layer
        

class UsdUtils:
    def __init(self):
        pass


    @staticmethod
    def CopyLayerMetadata(srcLayer: Sdf.Layer, dstLayer: Sdf.Layer):
        print("TODO: CopyLayerMetadata")


    @staticmethod
    def FindOrOpen(filepath: str):
        print("TODO: FindOrOpen")
        

    

class Usd:
    def __init__(self):
        pass

    class Stage:
        # enums
        LoadNone = 0    # Do not load `payload`

        def __init__(self):
            self.name = None
            self.startTimeCode = None
            self.endTimeCode = None
            pass


        @classmethod
        def CreateInMemory(cls):
            print("TODO: CreateInMemory")
            
        @classmethod
        def CreateNew(cls, filename: str):

            stage = cls()
            stage.name = filename
            print("Create!")

            return stage

        def SetStartTimeCode(t: int):
            self.startTimeCode = t

        def EndStartTimeCode(t: int):
            self.endTimeCode = t

        def GetRootLayer():
            print("TODO")
            root = Sdf.Layer()
            return root

class UsdGeom:
    def __init__(self):
        pass

    class Gprim:
        def __init__(self):
            self.path = None

        @staticmethod
        def CreateAttribute(name: str, ty: Sdf.ValueTypeNames):
            print("attr.name", name)
            print("attr.type", ty)

            pass

        @classmethod
        def Get(cls, path: str):
            gprim = cls()

            assert isinstance(gprim, Gprim)
            assert 0, "TODO"

            return gprim
            
        def GetDisplayColorAttr():
            print("TODO")
            

    class Xform(Gprim):
        def __init__(self):
            super().__init__()

        @classmethod
        def Define(cls, stage: Usd.Stage, path: str):
            assert isinstance(stage, Usd.Stage)

            prim = cls()
            prim.path = path

            return prim

    class Sphere(Gprim):
        def __init__(self):
            super().__init__()

            self.radius = 1.0

        @classmethod
        def Define(cls, stage: Usd.Stage, path: str):
            assert isinstance(stage, Usd.Stage)

            prim = cls()
            prim.path = path

            return prim
