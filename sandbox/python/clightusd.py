r"""Wrapper for c-lightusd.h

Generated with:
ctypesgen -lc-lightusd ../../src/c-lightusd.h -o clightusd.py

Do not modify this file.
"""

__docformat__ = "restructuredtext"

# Begin preamble for Python

import ctypes
import sys
from ctypes import *  # noqa: F401, F403

_int_types = (ctypes.c_int16, ctypes.c_int32)
if hasattr(ctypes, "c_int64"):
    # Some builds of ctypes apparently do not have ctypes.c_int64
    # defined; it's a pretty good bet that these builds do not
    # have 64-bit pointers.
    _int_types += (ctypes.c_int64,)
for t in _int_types:
    if ctypes.sizeof(t) == ctypes.sizeof(ctypes.c_size_t):
        c_ptrdiff_t = t
del t
del _int_types



class UserString:
    def __init__(self, seq):
        if isinstance(seq, bytes):
            self.data = seq
        elif isinstance(seq, UserString):
            self.data = seq.data[:]
        else:
            self.data = str(seq).encode()

    def __bytes__(self):
        return self.data

    def __str__(self):
        return self.data.decode()

    def __repr__(self):
        return repr(self.data)

    def __int__(self):
        return int(self.data.decode())

    def __long__(self):
        return int(self.data.decode())

    def __float__(self):
        return float(self.data.decode())

    def __complex__(self):
        return complex(self.data.decode())

    def __hash__(self):
        return hash(self.data)

    def __le__(self, string):
        if isinstance(string, UserString):
            return self.data <= string.data
        else:
            return self.data <= string

    def __lt__(self, string):
        if isinstance(string, UserString):
            return self.data < string.data
        else:
            return self.data < string

    def __ge__(self, string):
        if isinstance(string, UserString):
            return self.data >= string.data
        else:
            return self.data >= string

    def __gt__(self, string):
        if isinstance(string, UserString):
            return self.data > string.data
        else:
            return self.data > string

    def __eq__(self, string):
        if isinstance(string, UserString):
            return self.data == string.data
        else:
            return self.data == string

    def __ne__(self, string):
        if isinstance(string, UserString):
            return self.data != string.data
        else:
            return self.data != string

    def __contains__(self, char):
        return char in self.data

    def __len__(self):
        return len(self.data)

    def __getitem__(self, index):
        return self.__class__(self.data[index])

    def __getslice__(self, start, end):
        start = max(start, 0)
        end = max(end, 0)
        return self.__class__(self.data[start:end])

    def __add__(self, other):
        if isinstance(other, UserString):
            return self.__class__(self.data + other.data)
        elif isinstance(other, bytes):
            return self.__class__(self.data + other)
        else:
            return self.__class__(self.data + str(other).encode())

    def __radd__(self, other):
        if isinstance(other, bytes):
            return self.__class__(other + self.data)
        else:
            return self.__class__(str(other).encode() + self.data)

    def __mul__(self, n):
        return self.__class__(self.data * n)

    __rmul__ = __mul__

    def __mod__(self, args):
        return self.__class__(self.data % args)

    # the following methods are defined in alphabetical order:
    def capitalize(self):
        return self.__class__(self.data.capitalize())

    def center(self, width, *args):
        return self.__class__(self.data.center(width, *args))

    def count(self, sub, start=0, end=sys.maxsize):
        return self.data.count(sub, start, end)

    def decode(self, encoding=None, errors=None):  # XXX improve this?
        if encoding:
            if errors:
                return self.__class__(self.data.decode(encoding, errors))
            else:
                return self.__class__(self.data.decode(encoding))
        else:
            return self.__class__(self.data.decode())

    def encode(self, encoding=None, errors=None):  # XXX improve this?
        if encoding:
            if errors:
                return self.__class__(self.data.encode(encoding, errors))
            else:
                return self.__class__(self.data.encode(encoding))
        else:
            return self.__class__(self.data.encode())

    def endswith(self, suffix, start=0, end=sys.maxsize):
        return self.data.endswith(suffix, start, end)

    def expandtabs(self, tabsize=8):
        return self.__class__(self.data.expandtabs(tabsize))

    def find(self, sub, start=0, end=sys.maxsize):
        return self.data.find(sub, start, end)

    def index(self, sub, start=0, end=sys.maxsize):
        return self.data.index(sub, start, end)

    def isalpha(self):
        return self.data.isalpha()

    def isalnum(self):
        return self.data.isalnum()

    def isdecimal(self):
        return self.data.isdecimal()

    def isdigit(self):
        return self.data.isdigit()

    def islower(self):
        return self.data.islower()

    def isnumeric(self):
        return self.data.isnumeric()

    def isspace(self):
        return self.data.isspace()

    def istitle(self):
        return self.data.istitle()

    def isupper(self):
        return self.data.isupper()

    def join(self, seq):
        return self.data.join(seq)

    def ljust(self, width, *args):
        return self.__class__(self.data.ljust(width, *args))

    def lower(self):
        return self.__class__(self.data.lower())

    def lstrip(self, chars=None):
        return self.__class__(self.data.lstrip(chars))

    def partition(self, sep):
        return self.data.partition(sep)

    def replace(self, old, new, maxsplit=-1):
        return self.__class__(self.data.replace(old, new, maxsplit))

    def rfind(self, sub, start=0, end=sys.maxsize):
        return self.data.rfind(sub, start, end)

    def rindex(self, sub, start=0, end=sys.maxsize):
        return self.data.rindex(sub, start, end)

    def rjust(self, width, *args):
        return self.__class__(self.data.rjust(width, *args))

    def rpartition(self, sep):
        return self.data.rpartition(sep)

    def rstrip(self, chars=None):
        return self.__class__(self.data.rstrip(chars))

    def split(self, sep=None, maxsplit=-1):
        return self.data.split(sep, maxsplit)

    def rsplit(self, sep=None, maxsplit=-1):
        return self.data.rsplit(sep, maxsplit)

    def splitlines(self, keepends=0):
        return self.data.splitlines(keepends)

    def startswith(self, prefix, start=0, end=sys.maxsize):
        return self.data.startswith(prefix, start, end)

    def strip(self, chars=None):
        return self.__class__(self.data.strip(chars))

    def swapcase(self):
        return self.__class__(self.data.swapcase())

    def title(self):
        return self.__class__(self.data.title())

    def translate(self, *args):
        return self.__class__(self.data.translate(*args))

    def upper(self):
        return self.__class__(self.data.upper())

    def zfill(self, width):
        return self.__class__(self.data.zfill(width))


class MutableString(UserString):
    """mutable string objects

    Python strings are immutable objects.  This has the advantage, that
    strings may be used as dictionary keys.  If this property isn't needed
    and you insist on changing string values in place instead, you may cheat
    and use MutableString.

    But the purpose of this class is an educational one: to prevent
    people from inventing their own mutable string class derived
    from UserString and than forget thereby to remove (override) the
    __hash__ method inherited from UserString.  This would lead to
    errors that would be very hard to track down.

    A faster and better solution is to rewrite your program using lists."""

    def __init__(self, string=""):
        self.data = string

    def __hash__(self):
        raise TypeError("unhashable type (it is mutable)")

    def __setitem__(self, index, sub):
        if index < 0:
            index += len(self.data)
        if index < 0 or index >= len(self.data):
            raise IndexError
        self.data = self.data[:index] + sub + self.data[index + 1 :]

    def __delitem__(self, index):
        if index < 0:
            index += len(self.data)
        if index < 0 or index >= len(self.data):
            raise IndexError
        self.data = self.data[:index] + self.data[index + 1 :]

    def __setslice__(self, start, end, sub):
        start = max(start, 0)
        end = max(end, 0)
        if isinstance(sub, UserString):
            self.data = self.data[:start] + sub.data + self.data[end:]
        elif isinstance(sub, bytes):
            self.data = self.data[:start] + sub + self.data[end:]
        else:
            self.data = self.data[:start] + str(sub).encode() + self.data[end:]

    def __delslice__(self, start, end):
        start = max(start, 0)
        end = max(end, 0)
        self.data = self.data[:start] + self.data[end:]

    def immutable(self):
        return UserString(self.data)

    def __iadd__(self, other):
        if isinstance(other, UserString):
            self.data += other.data
        elif isinstance(other, bytes):
            self.data += other
        else:
            self.data += str(other).encode()
        return self

    def __imul__(self, n):
        self.data *= n
        return self


class String(MutableString, ctypes.Union):

    _fields_ = [("raw", ctypes.POINTER(ctypes.c_char)), ("data", ctypes.c_char_p)]

    def __init__(self, obj=b""):
        if isinstance(obj, (bytes, UserString)):
            self.data = bytes(obj)
        else:
            self.raw = obj

    def __len__(self):
        return self.data and len(self.data) or 0

    def from_param(cls, obj):
        # Convert None or 0
        if obj is None or obj == 0:
            return cls(ctypes.POINTER(ctypes.c_char)())

        # Convert from String
        elif isinstance(obj, String):
            return obj

        # Convert from bytes
        elif isinstance(obj, bytes):
            return cls(obj)

        # Convert from str
        elif isinstance(obj, str):
            return cls(obj.encode())

        # Convert from c_char_p
        elif isinstance(obj, ctypes.c_char_p):
            return obj

        # Convert from POINTER(ctypes.c_char)
        elif isinstance(obj, ctypes.POINTER(ctypes.c_char)):
            return obj

        # Convert from raw pointer
        elif isinstance(obj, int):
            return cls(ctypes.cast(obj, ctypes.POINTER(ctypes.c_char)))

        # Convert from ctypes.c_char array
        elif isinstance(obj, ctypes.c_char * len(obj)):
            return obj

        # Convert from object
        else:
            return String.from_param(obj._as_parameter_)

    from_param = classmethod(from_param)


def ReturnString(obj, func=None, arguments=None):
    return String.from_param(obj)


# As of ctypes 1.0, ctypes does not support custom error-checking
# functions on callbacks, nor does it support custom datatypes on
# callbacks, so we must ensure that all callbacks return
# primitive datatypes.
#
# Non-primitive return values wrapped with UNCHECKED won't be
# typechecked, and will be converted to ctypes.c_void_p.
def UNCHECKED(type):
    if hasattr(type, "_type_") and isinstance(type._type_, str) and type._type_ != "P":
        return type
    else:
        return ctypes.c_void_p


# ctypes doesn't have direct support for variadic functions, so we have to write
# our own wrapper class
class _variadic_function(object):
    def __init__(self, func, restype, argtypes, errcheck):
        self.func = func
        self.func.restype = restype
        self.argtypes = argtypes
        if errcheck:
            self.func.errcheck = errcheck

    def _as_parameter_(self):
        # So we can pass this variadic function as a function pointer
        return self.func

    def __call__(self, *args):
        fixed_args = []
        i = 0
        for argtype in self.argtypes:
            # Typecheck what we can
            fixed_args.append(argtype.from_param(args[i]))
            i += 1
        return self.func(*fixed_args + list(args[i:]))


def ord_if_char(value):
    """
    Simple helper used for casts to simple builtin types:  if the argument is a
    string type, it will be converted to it's ordinal value.

    This function will raise an exception if the argument is string with more
    than one characters.
    """
    return ord(value) if (isinstance(value, bytes) or isinstance(value, str)) else value

# End preamble

_libs = {}
_libdirs = []

# Begin loader

"""
Load libraries - appropriately for all our supported platforms
"""
# ----------------------------------------------------------------------------
# Copyright (c) 2008 David James
# Copyright (c) 2006-2008 Alex Holkner
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
#  * Neither the name of pyglet nor the names of its
#    contributors may be used to endorse or promote products
#    derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
# ----------------------------------------------------------------------------

import ctypes
import ctypes.util
import glob
import os.path
import platform
import re
import sys


def _environ_path(name):
    """Split an environment variable into a path-like list elements"""
    if name in os.environ:
        return os.environ[name].split(":")
    return []


class LibraryLoader:
    """
    A base class For loading of libraries ;-)
    Subclasses load libraries for specific platforms.
    """

    # library names formatted specifically for platforms
    name_formats = ["%s"]

    class Lookup:
        """Looking up calling conventions for a platform"""

        mode = ctypes.DEFAULT_MODE

        def __init__(self, path):
            super(LibraryLoader.Lookup, self).__init__()
            self.access = dict(cdecl=ctypes.CDLL(path, self.mode))

        def get(self, name, calling_convention="cdecl"):
            """Return the given name according to the selected calling convention"""
            if calling_convention not in self.access:
                raise LookupError(
                    "Unknown calling convention '{}' for function '{}'".format(
                        calling_convention, name
                    )
                )
            return getattr(self.access[calling_convention], name)

        def has(self, name, calling_convention="cdecl"):
            """Return True if this given calling convention finds the given 'name'"""
            if calling_convention not in self.access:
                return False
            return hasattr(self.access[calling_convention], name)

        def __getattr__(self, name):
            return getattr(self.access["cdecl"], name)

    def __init__(self):
        self.other_dirs = []

    def __call__(self, libname):
        """Given the name of a library, load it."""
        paths = self.getpaths(libname)

        for path in paths:
            # noinspection PyBroadException
            try:
                return self.Lookup(path)
            except Exception:  # pylint: disable=broad-except
                pass

        raise ImportError("Could not load %s." % libname)

    def getpaths(self, libname):
        """Return a list of paths where the library might be found."""
        if os.path.isabs(libname):
            yield libname
        else:
            # search through a prioritized series of locations for the library

            # we first search any specific directories identified by user
            for dir_i in self.other_dirs:
                for fmt in self.name_formats:
                    # dir_i should be absolute already
                    yield os.path.join(dir_i, fmt % libname)

            # check if this code is even stored in a physical file
            try:
                this_file = __file__
            except NameError:
                this_file = None

            # then we search the directory where the generated python interface is stored
            if this_file is not None:
                for fmt in self.name_formats:
                    yield os.path.abspath(os.path.join(os.path.dirname(__file__), fmt % libname))

            # now, use the ctypes tools to try to find the library
            for fmt in self.name_formats:
                path = ctypes.util.find_library(fmt % libname)
                if path:
                    yield path

            # then we search all paths identified as platform-specific lib paths
            for path in self.getplatformpaths(libname):
                yield path

            # Finally, we'll try the users current working directory
            for fmt in self.name_formats:
                yield os.path.abspath(os.path.join(os.path.curdir, fmt % libname))

    def getplatformpaths(self, _libname):  # pylint: disable=no-self-use
        """Return all the library paths available in this platform"""
        return []


# Darwin (Mac OS X)


class DarwinLibraryLoader(LibraryLoader):
    """Library loader for MacOS"""

    name_formats = [
        "lib%s.dylib",
        "lib%s.so",
        "lib%s.bundle",
        "%s.dylib",
        "%s.so",
        "%s.bundle",
        "%s",
    ]

    class Lookup(LibraryLoader.Lookup):
        """
        Looking up library files for this platform (Darwin aka MacOS)
        """

        # Darwin requires dlopen to be called with mode RTLD_GLOBAL instead
        # of the default RTLD_LOCAL.  Without this, you end up with
        # libraries not being loadable, resulting in "Symbol not found"
        # errors
        mode = ctypes.RTLD_GLOBAL

    def getplatformpaths(self, libname):
        if os.path.pathsep in libname:
            names = [libname]
        else:
            names = [fmt % libname for fmt in self.name_formats]

        for directory in self.getdirs(libname):
            for name in names:
                yield os.path.join(directory, name)

    @staticmethod
    def getdirs(libname):
        """Implements the dylib search as specified in Apple documentation:

        http://developer.apple.com/documentation/DeveloperTools/Conceptual/
            DynamicLibraries/Articles/DynamicLibraryUsageGuidelines.html

        Before commencing the standard search, the method first checks
        the bundle's ``Frameworks`` directory if the application is running
        within a bundle (OS X .app).
        """

        dyld_fallback_library_path = _environ_path("DYLD_FALLBACK_LIBRARY_PATH")
        if not dyld_fallback_library_path:
            dyld_fallback_library_path = [
                os.path.expanduser("~/lib"),
                "/usr/local/lib",
                "/usr/lib",
            ]

        dirs = []

        if "/" in libname:
            dirs.extend(_environ_path("DYLD_LIBRARY_PATH"))
        else:
            dirs.extend(_environ_path("LD_LIBRARY_PATH"))
            dirs.extend(_environ_path("DYLD_LIBRARY_PATH"))
            dirs.extend(_environ_path("LD_RUN_PATH"))

        if hasattr(sys, "frozen") and getattr(sys, "frozen") == "macosx_app":
            dirs.append(os.path.join(os.environ["RESOURCEPATH"], "..", "Frameworks"))

        dirs.extend(dyld_fallback_library_path)

        return dirs


# Posix


class PosixLibraryLoader(LibraryLoader):
    """Library loader for POSIX-like systems (including Linux)"""

    _ld_so_cache = None

    _include = re.compile(r"^\s*include\s+(?P<pattern>.*)")

    name_formats = ["lib%s.so", "%s.so", "%s"]

    class _Directories(dict):
        """Deal with directories"""

        def __init__(self):
            dict.__init__(self)
            self.order = 0

        def add(self, directory):
            """Add a directory to our current set of directories"""
            if len(directory) > 1:
                directory = directory.rstrip(os.path.sep)
            # only adds and updates order if exists and not already in set
            if not os.path.exists(directory):
                return
            order = self.setdefault(directory, self.order)
            if order == self.order:
                self.order += 1

        def extend(self, directories):
            """Add a list of directories to our set"""
            for a_dir in directories:
                self.add(a_dir)

        def ordered(self):
            """Sort the list of directories"""
            return (i[0] for i in sorted(self.items(), key=lambda d: d[1]))

    def _get_ld_so_conf_dirs(self, conf, dirs):
        """
        Recursive function to help parse all ld.so.conf files, including proper
        handling of the `include` directive.
        """

        try:
            with open(conf) as fileobj:
                for dirname in fileobj:
                    dirname = dirname.strip()
                    if not dirname:
                        continue

                    match = self._include.match(dirname)
                    if not match:
                        dirs.add(dirname)
                    else:
                        for dir2 in glob.glob(match.group("pattern")):
                            self._get_ld_so_conf_dirs(dir2, dirs)
        except IOError:
            pass

    def _create_ld_so_cache(self):
        # Recreate search path followed by ld.so.  This is going to be
        # slow to build, and incorrect (ld.so uses ld.so.cache, which may
        # not be up-to-date).  Used only as fallback for distros without
        # /sbin/ldconfig.
        #
        # We assume the DT_RPATH and DT_RUNPATH binary sections are omitted.

        directories = self._Directories()
        for name in (
            "LD_LIBRARY_PATH",
            "SHLIB_PATH",  # HP-UX
            "LIBPATH",  # OS/2, AIX
            "LIBRARY_PATH",  # BE/OS
        ):
            if name in os.environ:
                directories.extend(os.environ[name].split(os.pathsep))

        self._get_ld_so_conf_dirs("/etc/ld.so.conf", directories)

        bitage = platform.architecture()[0]

        unix_lib_dirs_list = []
        if bitage.startswith("64"):
            # prefer 64 bit if that is our arch
            unix_lib_dirs_list += ["/lib64", "/usr/lib64"]

        # must include standard libs, since those paths are also used by 64 bit
        # installs
        unix_lib_dirs_list += ["/lib", "/usr/lib"]
        if sys.platform.startswith("linux"):
            # Try and support multiarch work in Ubuntu
            # https://wiki.ubuntu.com/MultiarchSpec
            if bitage.startswith("32"):
                # Assume Intel/AMD x86 compat
                unix_lib_dirs_list += ["/lib/i386-linux-gnu", "/usr/lib/i386-linux-gnu"]
            elif bitage.startswith("64"):
                # Assume Intel/AMD x86 compatible
                unix_lib_dirs_list += [
                    "/lib/x86_64-linux-gnu",
                    "/usr/lib/x86_64-linux-gnu",
                ]
            else:
                # guess...
                unix_lib_dirs_list += glob.glob("/lib/*linux-gnu")
        directories.extend(unix_lib_dirs_list)

        cache = {}
        lib_re = re.compile(r"lib(.*)\.s[ol]")
        # ext_re = re.compile(r"\.s[ol]$")
        for our_dir in directories.ordered():
            try:
                for path in glob.glob("%s/*.s[ol]*" % our_dir):
                    file = os.path.basename(path)

                    # Index by filename
                    cache_i = cache.setdefault(file, set())
                    cache_i.add(path)

                    # Index by library name
                    match = lib_re.match(file)
                    if match:
                        library = match.group(1)
                        cache_i = cache.setdefault(library, set())
                        cache_i.add(path)
            except OSError:
                pass

        self._ld_so_cache = cache

    def getplatformpaths(self, libname):
        if self._ld_so_cache is None:
            self._create_ld_so_cache()

        result = self._ld_so_cache.get(libname, set())
        for i in result:
            # we iterate through all found paths for library, since we may have
            # actually found multiple architectures or other library types that
            # may not load
            yield i


# Windows


class WindowsLibraryLoader(LibraryLoader):
    """Library loader for Microsoft Windows"""

    name_formats = ["%s.dll", "lib%s.dll", "%slib.dll", "%s"]

    class Lookup(LibraryLoader.Lookup):
        """Lookup class for Windows libraries..."""

        def __init__(self, path):
            super(WindowsLibraryLoader.Lookup, self).__init__(path)
            self.access["stdcall"] = ctypes.windll.LoadLibrary(path)


# Platform switching

# If your value of sys.platform does not appear in this dict, please contact
# the Ctypesgen maintainers.

loaderclass = {
    "darwin": DarwinLibraryLoader,
    "cygwin": WindowsLibraryLoader,
    "win32": WindowsLibraryLoader,
    "msys": WindowsLibraryLoader,
}

load_library = loaderclass.get(sys.platform, PosixLibraryLoader)()


def add_library_search_dirs(other_dirs):
    """
    Add libraries to search paths.
    If library paths are relative, convert them to absolute with respect to this
    file's directory
    """
    for path in other_dirs:
        if not os.path.isabs(path):
            path = os.path.abspath(path)
        load_library.other_dirs.append(path)


del loaderclass

# End loader

add_library_search_dirs([])

# Begin libraries
_libs["c-lightusd"] = load_library("c-lightusd")

# 1 libraries
# End libraries

# No modules

__uint8_t = c_ubyte# /usr/include/x86_64-linux-gnu/bits/types.h: 38

__uint16_t = c_ushort# /usr/include/x86_64-linux-gnu/bits/types.h: 40

__uint32_t = c_uint# /usr/include/x86_64-linux-gnu/bits/types.h: 42

__uint64_t = c_ulong# /usr/include/x86_64-linux-gnu/bits/types.h: 45

uint8_t = __uint8_t# /usr/include/x86_64-linux-gnu/bits/stdint-uintn.h: 24

uint16_t = __uint16_t# /usr/include/x86_64-linux-gnu/bits/stdint-uintn.h: 25

uint32_t = __uint32_t# /usr/include/x86_64-linux-gnu/bits/stdint-uintn.h: 26

uint64_t = __uint64_t# /usr/include/x86_64-linux-gnu/bits/stdint-uintn.h: 27

# /mnt/data/work/lightusd/src/c-lightusd.h: 96
for _lib in _libs.values():
    if not _lib.has("c_lightusd_malloc", "cdecl"):
        continue
    c_lightusd_malloc = _lib.get("c_lightusd_malloc", "cdecl")
    c_lightusd_malloc.argtypes = [c_size_t]
    c_lightusd_malloc.restype = POINTER(c_ubyte)
    c_lightusd_malloc.errcheck = lambda v,*a : cast(v, c_void_p)
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 99
for _lib in _libs.values():
    if not _lib.has("c_lightusd_free", "cdecl"):
        continue
    c_lightusd_free = _lib.get("c_lightusd_free", "cdecl")
    c_lightusd_free.argtypes = [POINTER(None)]
    c_lightusd_free.restype = c_int
    break

enum_anon_4 = c_int# /mnt/data/work/lightusd/src/c-lightusd.h: 113

C_LIGHTUSD_FORMAT_UNKNOWN = 0# /mnt/data/work/lightusd/src/c-lightusd.h: 113

C_LIGHTUSD_FORMAT_AUTO = (C_LIGHTUSD_FORMAT_UNKNOWN + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 113

C_LIGHTUSD_FORMAT_USDA = (C_LIGHTUSD_FORMAT_AUTO + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 113

C_LIGHTUSD_FORMAT_USDC = (C_LIGHTUSD_FORMAT_USDA + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 113

C_LIGHTUSD_FORMAT_USDZ = (C_LIGHTUSD_FORMAT_USDC + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 113

CLightUSDFormat = enum_anon_4# /mnt/data/work/lightusd/src/c-lightusd.h: 113

enum_anon_5 = c_int# /mnt/data/work/lightusd/src/c-lightusd.h: 120

C_LIGHTUSD_AXIS_UNKNOWN = 0# /mnt/data/work/lightusd/src/c-lightusd.h: 120

C_LIGHTUSD_AXIS_X = (C_LIGHTUSD_AXIS_UNKNOWN + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 120

C_LIGHTUSD_AXIS_Y = (C_LIGHTUSD_AXIS_X + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 120

C_LIGHTUSD_AXIS_Z = (C_LIGHTUSD_AXIS_Y + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 120

CLightUSDAxis = enum_anon_5# /mnt/data/work/lightusd/src/c-lightusd.h: 120

enum_anon_6 = c_int# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_UNKNOWN = 0# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TOKEN = (C_LIGHTUSD_VALUE_UNKNOWN + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TOKEN_VECTOR = (C_LIGHTUSD_VALUE_TOKEN + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_STRING = (C_LIGHTUSD_VALUE_TOKEN_VECTOR + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_STRING_VECTOR = (C_LIGHTUSD_VALUE_STRING + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_BOOL = (C_LIGHTUSD_VALUE_STRING_VECTOR + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_HALF = (C_LIGHTUSD_VALUE_BOOL + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_HALF2 = (C_LIGHTUSD_VALUE_HALF + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_HALF3 = (C_LIGHTUSD_VALUE_HALF2 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_HALF4 = (C_LIGHTUSD_VALUE_HALF3 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_INT = (C_LIGHTUSD_VALUE_HALF4 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_INT2 = (C_LIGHTUSD_VALUE_INT + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_INT3 = (C_LIGHTUSD_VALUE_INT2 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_INT4 = (C_LIGHTUSD_VALUE_INT3 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_UINT = (C_LIGHTUSD_VALUE_INT4 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_UINT2 = (C_LIGHTUSD_VALUE_UINT + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_UINT3 = (C_LIGHTUSD_VALUE_UINT2 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_UINT4 = (C_LIGHTUSD_VALUE_UINT3 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_INT64 = (C_LIGHTUSD_VALUE_UINT4 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_UINT64 = (C_LIGHTUSD_VALUE_INT64 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_FLOAT = (C_LIGHTUSD_VALUE_UINT64 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_FLOAT2 = (C_LIGHTUSD_VALUE_FLOAT + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_FLOAT3 = (C_LIGHTUSD_VALUE_FLOAT2 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_FLOAT4 = (C_LIGHTUSD_VALUE_FLOAT3 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_DOUBLE = (C_LIGHTUSD_VALUE_FLOAT4 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_DOUBLE2 = (C_LIGHTUSD_VALUE_DOUBLE + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_DOUBLE3 = (C_LIGHTUSD_VALUE_DOUBLE2 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_DOUBLE4 = (C_LIGHTUSD_VALUE_DOUBLE3 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_QUATH = (C_LIGHTUSD_VALUE_DOUBLE4 + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_QUATF = (C_LIGHTUSD_VALUE_QUATH + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_QUATD = (C_LIGHTUSD_VALUE_QUATF + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_COLOR3H = (C_LIGHTUSD_VALUE_QUATD + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_COLOR3F = (C_LIGHTUSD_VALUE_COLOR3H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_COLOR3D = (C_LIGHTUSD_VALUE_COLOR3F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_COLOR4H = (C_LIGHTUSD_VALUE_COLOR3D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_COLOR4F = (C_LIGHTUSD_VALUE_COLOR4H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_COLOR4D = (C_LIGHTUSD_VALUE_COLOR4F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TEXCOORD2H = (C_LIGHTUSD_VALUE_COLOR4D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TEXCOORD2F = (C_LIGHTUSD_VALUE_TEXCOORD2H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TEXCOORD2D = (C_LIGHTUSD_VALUE_TEXCOORD2F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TEXCOORD3H = (C_LIGHTUSD_VALUE_TEXCOORD2D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TEXCOORD3F = (C_LIGHTUSD_VALUE_TEXCOORD3H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_TEXCOORD3D = (C_LIGHTUSD_VALUE_TEXCOORD3F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_NORMAL3H = (C_LIGHTUSD_VALUE_TEXCOORD3D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_NORMAL3F = (C_LIGHTUSD_VALUE_NORMAL3H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_NORMAL3D = (C_LIGHTUSD_VALUE_NORMAL3F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_VECTOR3H = (C_LIGHTUSD_VALUE_NORMAL3D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_VECTOR3F = (C_LIGHTUSD_VALUE_VECTOR3H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_VECTOR3D = (C_LIGHTUSD_VALUE_VECTOR3F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_POINT3H = (C_LIGHTUSD_VALUE_VECTOR3D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_POINT3F = (C_LIGHTUSD_VALUE_POINT3H + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_POINT3D = (C_LIGHTUSD_VALUE_POINT3F + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_MATRIX2D = (C_LIGHTUSD_VALUE_POINT3D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_MATRIX3D = (C_LIGHTUSD_VALUE_MATRIX2D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_MATRIX4D = (C_LIGHTUSD_VALUE_MATRIX3D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_FRAME4D = (C_LIGHTUSD_VALUE_MATRIX4D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_DICTIONARY = (C_LIGHTUSD_VALUE_FRAME4D + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

C_LIGHTUSD_VALUE_END = (C_LIGHTUSD_VALUE_DICTIONARY + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 189

CLightUSDValueType = enum_anon_6# /mnt/data/work/lightusd/src/c-lightusd.h: 189

enum_anon_7 = c_int# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_UNKNOWN = 0# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_MODEL = (C_LIGHTUSD_PRIM_UNKNOWN + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_SCOPE = (C_LIGHTUSD_PRIM_MODEL + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_XFORM = (C_LIGHTUSD_PRIM_SCOPE + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_MESH = (C_LIGHTUSD_PRIM_XFORM + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_GEOMSUBSET = (C_LIGHTUSD_PRIM_MESH + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_MATERIAL = (C_LIGHTUSD_PRIM_GEOMSUBSET + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_SHADER = (C_LIGHTUSD_PRIM_MATERIAL + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_CAMERA = (C_LIGHTUSD_PRIM_SHADER + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_SPHERE_LIGHT = (C_LIGHTUSD_PRIM_CAMERA + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_DISTANT_LIGHT = (C_LIGHTUSD_PRIM_SPHERE_LIGHT + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_RECT_LIGHT = (C_LIGHTUSD_PRIM_DISTANT_LIGHT + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

C_LIGHTUSD_PRIM_END = (C_LIGHTUSD_PRIM_RECT_LIGHT + 1)# /mnt/data/work/lightusd/src/c-lightusd.h: 210

CLightUSDPrimType = enum_anon_7# /mnt/data/work/lightusd/src/c-lightusd.h: 210

c_lightusd_half_t = uint16_t# /mnt/data/work/lightusd/src/c-lightusd.h: 216

# /mnt/data/work/lightusd/src/c-lightusd.h: 225
class struct_anon_8(Structure):
    pass

struct_anon_8.__slots__ = [
    'x',
    'y',
]
struct_anon_8._fields_ = [
    ('x', c_int),
    ('y', c_int),
]

c_lightusd_int2_t = struct_anon_8# /mnt/data/work/lightusd/src/c-lightusd.h: 225

# /mnt/data/work/lightusd/src/c-lightusd.h: 231
class struct_anon_9(Structure):
    pass

struct_anon_9.__slots__ = [
    'x',
    'y',
    'z',
]
struct_anon_9._fields_ = [
    ('x', c_int),
    ('y', c_int),
    ('z', c_int),
]

c_lightusd_int3_t = struct_anon_9# /mnt/data/work/lightusd/src/c-lightusd.h: 231

# /mnt/data/work/lightusd/src/c-lightusd.h: 238
class struct_anon_10(Structure):
    pass

struct_anon_10.__slots__ = [
    'x',
    'y',
    'z',
    'w',
]
struct_anon_10._fields_ = [
    ('x', c_int),
    ('y', c_int),
    ('z', c_int),
    ('w', c_int),
]

c_lightusd_int4_t = struct_anon_10# /mnt/data/work/lightusd/src/c-lightusd.h: 238

# /mnt/data/work/lightusd/src/c-lightusd.h: 243
class struct_anon_11(Structure):
    pass

struct_anon_11.__slots__ = [
    'x',
    'y',
]
struct_anon_11._fields_ = [
    ('x', uint32_t),
    ('y', uint32_t),
]

c_lightusd_uint2_t = struct_anon_11# /mnt/data/work/lightusd/src/c-lightusd.h: 243

# /mnt/data/work/lightusd/src/c-lightusd.h: 249
class struct_anon_12(Structure):
    pass

struct_anon_12.__slots__ = [
    'x',
    'y',
    'z',
]
struct_anon_12._fields_ = [
    ('x', uint32_t),
    ('y', uint32_t),
    ('z', uint32_t),
]

c_lightusd_uint3_t = struct_anon_12# /mnt/data/work/lightusd/src/c-lightusd.h: 249

# /mnt/data/work/lightusd/src/c-lightusd.h: 256
class struct_anon_13(Structure):
    pass

struct_anon_13.__slots__ = [
    'x',
    'y',
    'z',
    'w',
]
struct_anon_13._fields_ = [
    ('x', uint32_t),
    ('y', uint32_t),
    ('z', uint32_t),
    ('w', uint32_t),
]

c_lightusd_uint4_t = struct_anon_13# /mnt/data/work/lightusd/src/c-lightusd.h: 256

# /mnt/data/work/lightusd/src/c-lightusd.h: 261
class struct_anon_14(Structure):
    pass

struct_anon_14.__slots__ = [
    'x',
    'y',
]
struct_anon_14._fields_ = [
    ('x', c_lightusd_half_t),
    ('y', c_lightusd_half_t),
]

c_lightusd_half2_t = struct_anon_14# /mnt/data/work/lightusd/src/c-lightusd.h: 261

# /mnt/data/work/lightusd/src/c-lightusd.h: 267
class struct_anon_15(Structure):
    pass

struct_anon_15.__slots__ = [
    'x',
    'y',
    'z',
]
struct_anon_15._fields_ = [
    ('x', c_lightusd_half_t),
    ('y', c_lightusd_half_t),
    ('z', c_lightusd_half_t),
]

c_lightusd_half3_t = struct_anon_15# /mnt/data/work/lightusd/src/c-lightusd.h: 267

# /mnt/data/work/lightusd/src/c-lightusd.h: 274
class struct_anon_16(Structure):
    pass

struct_anon_16.__slots__ = [
    'x',
    'y',
    'z',
    'w',
]
struct_anon_16._fields_ = [
    ('x', c_lightusd_half_t),
    ('y', c_lightusd_half_t),
    ('z', c_lightusd_half_t),
    ('w', c_lightusd_half_t),
]

c_lightusd_half4_t = struct_anon_16# /mnt/data/work/lightusd/src/c-lightusd.h: 274

# /mnt/data/work/lightusd/src/c-lightusd.h: 279
class struct_anon_17(Structure):
    pass

struct_anon_17.__slots__ = [
    'x',
    'y',
]
struct_anon_17._fields_ = [
    ('x', c_float),
    ('y', c_float),
]

c_lightusd_float2_t = struct_anon_17# /mnt/data/work/lightusd/src/c-lightusd.h: 279

# /mnt/data/work/lightusd/src/c-lightusd.h: 285
class struct_anon_18(Structure):
    pass

struct_anon_18.__slots__ = [
    'x',
    'y',
    'z',
]
struct_anon_18._fields_ = [
    ('x', c_float),
    ('y', c_float),
    ('z', c_float),
]

c_lightusd_float3_t = struct_anon_18# /mnt/data/work/lightusd/src/c-lightusd.h: 285

# /mnt/data/work/lightusd/src/c-lightusd.h: 292
class struct_anon_19(Structure):
    pass

struct_anon_19.__slots__ = [
    'x',
    'y',
    'z',
    'w',
]
struct_anon_19._fields_ = [
    ('x', c_float),
    ('y', c_float),
    ('z', c_float),
    ('w', c_float),
]

c_lightusd_float4_t = struct_anon_19# /mnt/data/work/lightusd/src/c-lightusd.h: 292

# /mnt/data/work/lightusd/src/c-lightusd.h: 297
class struct_anon_20(Structure):
    pass

struct_anon_20.__slots__ = [
    'x',
    'y',
]
struct_anon_20._fields_ = [
    ('x', c_double),
    ('y', c_double),
]

c_lightusd_double2_t = struct_anon_20# /mnt/data/work/lightusd/src/c-lightusd.h: 297

# /mnt/data/work/lightusd/src/c-lightusd.h: 303
class struct_anon_21(Structure):
    pass

struct_anon_21.__slots__ = [
    'x',
    'y',
    'z',
]
struct_anon_21._fields_ = [
    ('x', c_double),
    ('y', c_double),
    ('z', c_double),
]

c_lightusd_double3_t = struct_anon_21# /mnt/data/work/lightusd/src/c-lightusd.h: 303

# /mnt/data/work/lightusd/src/c-lightusd.h: 310
class struct_anon_22(Structure):
    pass

struct_anon_22.__slots__ = [
    'x',
    'y',
    'z',
    'w',
]
struct_anon_22._fields_ = [
    ('x', c_double),
    ('y', c_double),
    ('z', c_double),
    ('w', c_double),
]

c_lightusd_double4_t = struct_anon_22# /mnt/data/work/lightusd/src/c-lightusd.h: 310

# /mnt/data/work/lightusd/src/c-lightusd.h: 314
class struct_anon_23(Structure):
    pass

struct_anon_23.__slots__ = [
    'm',
]
struct_anon_23._fields_ = [
    ('m', c_double * int(4)),
]

c_lightusd_matrix2d_t = struct_anon_23# /mnt/data/work/lightusd/src/c-lightusd.h: 314

# /mnt/data/work/lightusd/src/c-lightusd.h: 318
class struct_anon_24(Structure):
    pass

struct_anon_24.__slots__ = [
    'm',
]
struct_anon_24._fields_ = [
    ('m', c_double * int(9)),
]

c_lightusd_matrix3d_t = struct_anon_24# /mnt/data/work/lightusd/src/c-lightusd.h: 318

# /mnt/data/work/lightusd/src/c-lightusd.h: 322
class struct_anon_25(Structure):
    pass

struct_anon_25.__slots__ = [
    'm',
]
struct_anon_25._fields_ = [
    ('m', c_double * int(16)),
]

c_lightusd_matrix4d_t = struct_anon_25# /mnt/data/work/lightusd/src/c-lightusd.h: 322

# /mnt/data/work/lightusd/src/c-lightusd.h: 327
class struct_anon_26(Structure):
    pass

struct_anon_26.__slots__ = [
    'imag',
    'real',
]
struct_anon_26._fields_ = [
    ('imag', c_lightusd_half_t * int(3)),
    ('real', c_lightusd_half_t),
]

c_lightusd_quath_t = struct_anon_26# /mnt/data/work/lightusd/src/c-lightusd.h: 327

# /mnt/data/work/lightusd/src/c-lightusd.h: 332
class struct_anon_27(Structure):
    pass

struct_anon_27.__slots__ = [
    'imag',
    'real',
]
struct_anon_27._fields_ = [
    ('imag', c_float * int(3)),
    ('real', c_float),
]

c_lightusd_quatf_t = struct_anon_27# /mnt/data/work/lightusd/src/c-lightusd.h: 332

# /mnt/data/work/lightusd/src/c-lightusd.h: 337
class struct_anon_28(Structure):
    pass

struct_anon_28.__slots__ = [
    'imag',
    'real',
]
struct_anon_28._fields_ = [
    ('imag', c_double * int(3)),
    ('real', c_double),
]

c_lightusd_quatd_t = struct_anon_28# /mnt/data/work/lightusd/src/c-lightusd.h: 337

c_lightusd_color3h_t = c_lightusd_half3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 339

c_lightusd_color3f_t = c_lightusd_float3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 340

c_lightusd_color3d_t = c_lightusd_double3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 341

c_lightusd_color4h_t = c_lightusd_half4_t# /mnt/data/work/lightusd/src/c-lightusd.h: 343

c_lightusd_color4f_t = c_lightusd_float4_t# /mnt/data/work/lightusd/src/c-lightusd.h: 344

c_lightusd_color4d_t = c_lightusd_double4_t# /mnt/data/work/lightusd/src/c-lightusd.h: 345

c_lightusd_point3h_t = c_lightusd_half3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 347

c_lightusd_point3f_t = c_lightusd_float3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 348

c_lightusd_point3d_t = c_lightusd_double3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 349

c_lightusd_normal3h_t = c_lightusd_half3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 351

c_lightusd_normal3f_t = c_lightusd_float3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 352

c_lightusd_normal3d_t = c_lightusd_double3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 353

c_lightusd_vector3h_t = c_lightusd_half3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 355

c_lightusd_vector3f_t = c_lightusd_float3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 356

c_lightusd_vector3d_t = c_lightusd_double3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 357

c_lightusd_frame4d_t = c_lightusd_matrix4d_t# /mnt/data/work/lightusd/src/c-lightusd.h: 359

c_lightusd_texcoord2h_t = c_lightusd_half2_t# /mnt/data/work/lightusd/src/c-lightusd.h: 361

c_lightusd_texcoord2f_t = c_lightusd_float2_t# /mnt/data/work/lightusd/src/c-lightusd.h: 362

c_lightusd_texcoord2d_t = c_lightusd_double2_t# /mnt/data/work/lightusd/src/c-lightusd.h: 363

c_lightusd_texcoord3h_t = c_lightusd_half3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 365

c_lightusd_texcoord3f_t = c_lightusd_float3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 366

c_lightusd_texcoord3d_t = c_lightusd_double3_t# /mnt/data/work/lightusd/src/c-lightusd.h: 367

# /mnt/data/work/lightusd/src/c-lightusd.h: 369
class struct_c_lightusd_token_t(Structure):
    pass

c_lightusd_token_t = struct_c_lightusd_token_t# /mnt/data/work/lightusd/src/c-lightusd.h: 369

# /mnt/data/work/lightusd/src/c-lightusd.h: 371
if _libs["c-lightusd"].has("c_lightusd_token_new", "cdecl"):
    c_lightusd_token_new = _libs["c-lightusd"].get("c_lightusd_token_new", "cdecl")
    c_lightusd_token_new.argtypes = [String]
    c_lightusd_token_new.restype = POINTER(c_lightusd_token_t)

# /mnt/data/work/lightusd/src/c-lightusd.h: 374
if _libs["c-lightusd"].has("c_lightusd_token_dup", "cdecl"):
    c_lightusd_token_dup = _libs["c-lightusd"].get("c_lightusd_token_dup", "cdecl")
    c_lightusd_token_dup.argtypes = [POINTER(c_lightusd_token_t)]
    c_lightusd_token_dup.restype = POINTER(c_lightusd_token_t)

# /mnt/data/work/lightusd/src/c-lightusd.h: 378
if _libs["c-lightusd"].has("c_lightusd_token_size", "cdecl"):
    c_lightusd_token_size = _libs["c-lightusd"].get("c_lightusd_token_size", "cdecl")
    c_lightusd_token_size.argtypes = [POINTER(c_lightusd_token_t)]
    c_lightusd_token_size.restype = c_size_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 384
if _libs["c-lightusd"].has("c_lightusd_token_str", "cdecl"):
    c_lightusd_token_str = _libs["c-lightusd"].get("c_lightusd_token_str", "cdecl")
    c_lightusd_token_str.argtypes = [POINTER(c_lightusd_token_t)]
    c_lightusd_token_str.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 390
if _libs["c-lightusd"].has("c_lightusd_token_free", "cdecl"):
    c_lightusd_token_free = _libs["c-lightusd"].get("c_lightusd_token_free", "cdecl")
    c_lightusd_token_free.argtypes = [POINTER(c_lightusd_token_t)]
    c_lightusd_token_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 393
class struct_c_lightusd_token_vector_t(Structure):
    pass

c_lightusd_token_vector_t = struct_c_lightusd_token_vector_t# /mnt/data/work/lightusd/src/c-lightusd.h: 393

# /mnt/data/work/lightusd/src/c-lightusd.h: 397
if _libs["c-lightusd"].has("c_lightusd_token_vector_new_empty", "cdecl"):
    c_lightusd_token_vector_new_empty = _libs["c-lightusd"].get("c_lightusd_token_vector_new_empty", "cdecl")
    c_lightusd_token_vector_new_empty.argtypes = []
    c_lightusd_token_vector_new_empty.restype = POINTER(c_lightusd_token_vector_t)

# /mnt/data/work/lightusd/src/c-lightusd.h: 401
if _libs["c-lightusd"].has("c_lightusd_token_vector_new", "cdecl"):
    c_lightusd_token_vector_new = _libs["c-lightusd"].get("c_lightusd_token_vector_new", "cdecl")
    c_lightusd_token_vector_new.argtypes = [c_size_t, POINTER(POINTER(c_char))]
    c_lightusd_token_vector_new.restype = POINTER(c_lightusd_token_vector_t)

# /mnt/data/work/lightusd/src/c-lightusd.h: 403
if _libs["c-lightusd"].has("c_lightusd_token_vector_free", "cdecl"):
    c_lightusd_token_vector_free = _libs["c-lightusd"].get("c_lightusd_token_vector_free", "cdecl")
    c_lightusd_token_vector_free.argtypes = [POINTER(c_lightusd_token_vector_t)]
    c_lightusd_token_vector_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 410
if _libs["c-lightusd"].has("c_lightusd_token_vector_size", "cdecl"):
    c_lightusd_token_vector_size = _libs["c-lightusd"].get("c_lightusd_token_vector_size", "cdecl")
    c_lightusd_token_vector_size.argtypes = [POINTER(c_lightusd_token_vector_t)]
    c_lightusd_token_vector_size.restype = c_size_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 412
if _libs["c-lightusd"].has("c_lightusd_token_vector_clear", "cdecl"):
    c_lightusd_token_vector_clear = _libs["c-lightusd"].get("c_lightusd_token_vector_clear", "cdecl")
    c_lightusd_token_vector_clear.argtypes = [POINTER(c_lightusd_token_vector_t)]
    c_lightusd_token_vector_clear.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 413
if _libs["c-lightusd"].has("c_lightusd_token_vector_resize", "cdecl"):
    c_lightusd_token_vector_resize = _libs["c-lightusd"].get("c_lightusd_token_vector_resize", "cdecl")
    c_lightusd_token_vector_resize.argtypes = [POINTER(c_lightusd_token_vector_t), c_size_t]
    c_lightusd_token_vector_resize.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 420
if _libs["c-lightusd"].has("c_lightusd_token_vector_str", "cdecl"):
    c_lightusd_token_vector_str = _libs["c-lightusd"].get("c_lightusd_token_vector_str", "cdecl")
    c_lightusd_token_vector_str.argtypes = [POINTER(c_lightusd_token_vector_t), c_size_t]
    c_lightusd_token_vector_str.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 427
if _libs["c-lightusd"].has("c_lightusd_token_vector_replace", "cdecl"):
    c_lightusd_token_vector_replace = _libs["c-lightusd"].get("c_lightusd_token_vector_replace", "cdecl")
    c_lightusd_token_vector_replace.argtypes = [POINTER(c_lightusd_token_vector_t), c_size_t, String]
    c_lightusd_token_vector_replace.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 431
class struct_c_lightusd_string_t(Structure):
    pass

c_lightusd_string_t = struct_c_lightusd_string_t# /mnt/data/work/lightusd/src/c-lightusd.h: 431

# /mnt/data/work/lightusd/src/c-lightusd.h: 433
if _libs["c-lightusd"].has("c_lightusd_string_new_empty", "cdecl"):
    c_lightusd_string_new_empty = _libs["c-lightusd"].get("c_lightusd_string_new_empty", "cdecl")
    c_lightusd_string_new_empty.argtypes = []
    c_lightusd_string_new_empty.restype = POINTER(c_lightusd_string_t)

# /mnt/data/work/lightusd/src/c-lightusd.h: 435
if _libs["c-lightusd"].has("c_lightusd_string_new", "cdecl"):
    c_lightusd_string_new = _libs["c-lightusd"].get("c_lightusd_string_new", "cdecl")
    c_lightusd_string_new.argtypes = [String]
    c_lightusd_string_new.restype = POINTER(c_lightusd_string_t)

# /mnt/data/work/lightusd/src/c-lightusd.h: 438
if _libs["c-lightusd"].has("c_lightusd_string_size", "cdecl"):
    c_lightusd_string_size = _libs["c-lightusd"].get("c_lightusd_string_size", "cdecl")
    c_lightusd_string_size.argtypes = [POINTER(c_lightusd_string_t)]
    c_lightusd_string_size.restype = c_size_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 444
if _libs["c-lightusd"].has("c_lightusd_string_replace", "cdecl"):
    c_lightusd_string_replace = _libs["c-lightusd"].get("c_lightusd_string_replace", "cdecl")
    c_lightusd_string_replace.argtypes = [POINTER(c_lightusd_string_t), String]
    c_lightusd_string_replace.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 450
if _libs["c-lightusd"].has("c_lightusd_string_str", "cdecl"):
    c_lightusd_string_str = _libs["c-lightusd"].get("c_lightusd_string_str", "cdecl")
    c_lightusd_string_str.argtypes = [POINTER(c_lightusd_string_t)]
    c_lightusd_string_str.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 452
if _libs["c-lightusd"].has("c_lightusd_string_free", "cdecl"):
    c_lightusd_string_free = _libs["c-lightusd"].get("c_lightusd_string_free", "cdecl")
    c_lightusd_string_free.argtypes = [POINTER(c_lightusd_string_t)]
    c_lightusd_string_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 456
class struct_anon_29(Structure):
    pass

struct_anon_29.__slots__ = [
    'data',
]
struct_anon_29._fields_ = [
    ('data', POINTER(None)),
]

c_lightusd_string_vector = struct_anon_29# /mnt/data/work/lightusd/src/c-lightusd.h: 456

# /mnt/data/work/lightusd/src/c-lightusd.h: 459
if _libs["c-lightusd"].has("c_lightusd_string_vector_new_empty", "cdecl"):
    c_lightusd_string_vector_new_empty = _libs["c-lightusd"].get("c_lightusd_string_vector_new_empty", "cdecl")
    c_lightusd_string_vector_new_empty.argtypes = [POINTER(c_lightusd_string_vector), c_size_t]
    c_lightusd_string_vector_new_empty.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 462
if _libs["c-lightusd"].has("c_lightusd_string_vector_new", "cdecl"):
    c_lightusd_string_vector_new = _libs["c-lightusd"].get("c_lightusd_string_vector_new", "cdecl")
    c_lightusd_string_vector_new.argtypes = [POINTER(c_lightusd_string_vector), c_size_t, POINTER(POINTER(c_char))]
    c_lightusd_string_vector_new.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 471
if _libs["c-lightusd"].has("c_lightusd_string_vector_size", "cdecl"):
    c_lightusd_string_vector_size = _libs["c-lightusd"].get("c_lightusd_string_vector_size", "cdecl")
    c_lightusd_string_vector_size.argtypes = [POINTER(c_lightusd_string_vector)]
    c_lightusd_string_vector_size.restype = c_size_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 473
if _libs["c-lightusd"].has("c_lightusd_string_vector_clear", "cdecl"):
    c_lightusd_string_vector_clear = _libs["c-lightusd"].get("c_lightusd_string_vector_clear", "cdecl")
    c_lightusd_string_vector_clear.argtypes = [POINTER(c_lightusd_string_vector)]
    c_lightusd_string_vector_clear.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 474
if _libs["c-lightusd"].has("c_lightusd_string_vector_resize", "cdecl"):
    c_lightusd_string_vector_resize = _libs["c-lightusd"].get("c_lightusd_string_vector_resize", "cdecl")
    c_lightusd_string_vector_resize.argtypes = [POINTER(c_lightusd_string_vector), c_size_t]
    c_lightusd_string_vector_resize.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 481
if _libs["c-lightusd"].has("c_lightusd_string_vector_str", "cdecl"):
    c_lightusd_string_vector_str = _libs["c-lightusd"].get("c_lightusd_string_vector_str", "cdecl")
    c_lightusd_string_vector_str.argtypes = [POINTER(c_lightusd_string_vector), c_size_t]
    c_lightusd_string_vector_str.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 488
if _libs["c-lightusd"].has("c_lightusd_string_vector_replace", "cdecl"):
    c_lightusd_string_vector_replace = _libs["c-lightusd"].get("c_lightusd_string_vector_replace", "cdecl")
    c_lightusd_string_vector_replace.argtypes = [POINTER(c_lightusd_string_vector), c_size_t, String]
    c_lightusd_string_vector_replace.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 491
if _libs["c-lightusd"].has("c_lightusd_string_vector_free", "cdecl"):
    c_lightusd_string_vector_free = _libs["c-lightusd"].get("c_lightusd_string_vector_free", "cdecl")
    c_lightusd_string_vector_free.argtypes = [POINTER(c_lightusd_string_vector)]
    c_lightusd_string_vector_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 498
if _libs["c-lightusd"].has("c_lightusd_prim_type_name", "cdecl"):
    c_lightusd_prim_type_name = _libs["c-lightusd"].get("c_lightusd_prim_type_name", "cdecl")
    c_lightusd_prim_type_name.argtypes = [CLightUSDPrimType]
    c_lightusd_prim_type_name.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 505
if _libs["c-lightusd"].has("c_lightusd_prim_type_from_string", "cdecl"):
    c_lightusd_prim_type_from_string = _libs["c-lightusd"].get("c_lightusd_prim_type_from_string", "cdecl")
    c_lightusd_prim_type_from_string.argtypes = [String]
    c_lightusd_prim_type_from_string.restype = CLightUSDPrimType

# /mnt/data/work/lightusd/src/c-lightusd.h: 512
if _libs["c-lightusd"].has("c_lightusd_value_type_name", "cdecl"):
    c_lightusd_value_type_name = _libs["c-lightusd"].get("c_lightusd_value_type_name", "cdecl")
    c_lightusd_value_type_name.argtypes = [CLightUSDValueType]
    c_lightusd_value_type_name.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 518
if _libs["c-lightusd"].has("c_lightusd_value_type_is_numeric", "cdecl"):
    c_lightusd_value_type_is_numeric = _libs["c-lightusd"].get("c_lightusd_value_type_is_numeric", "cdecl")
    c_lightusd_value_type_is_numeric.argtypes = [CLightUSDValueType]
    c_lightusd_value_type_is_numeric.restype = uint32_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 525
if _libs["c-lightusd"].has("c_lightusd_value_type_sizeof", "cdecl"):
    c_lightusd_value_type_sizeof = _libs["c-lightusd"].get("c_lightusd_value_type_sizeof", "cdecl")
    c_lightusd_value_type_sizeof.argtypes = [CLightUSDValueType]
    c_lightusd_value_type_sizeof.restype = uint32_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 534
if _libs["c-lightusd"].has("c_lightusd_value_type_components", "cdecl"):
    c_lightusd_value_type_components = _libs["c-lightusd"].get("c_lightusd_value_type_components", "cdecl")
    c_lightusd_value_type_components.argtypes = [CLightUSDValueType]
    c_lightusd_value_type_components.restype = uint32_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 537
class struct_CLightUSDValue(Structure):
    pass

CLightUSDValue = struct_CLightUSDValue# /mnt/data/work/lightusd/src/c-lightusd.h: 537

# /mnt/data/work/lightusd/src/c-lightusd.h: 542
if _libs["c-lightusd"].has("c_lightusd_value_type", "cdecl"):
    c_lightusd_value_type = _libs["c-lightusd"].get("c_lightusd_value_type", "cdecl")
    c_lightusd_value_type.argtypes = [POINTER(CLightUSDValue)]
    c_lightusd_value_type.restype = CLightUSDValueType

# /mnt/data/work/lightusd/src/c-lightusd.h: 547
if _libs["c-lightusd"].has("c_lightusd_value_new_null", "cdecl"):
    c_lightusd_value_new_null = _libs["c-lightusd"].get("c_lightusd_value_new_null", "cdecl")
    c_lightusd_value_new_null.argtypes = []
    c_lightusd_value_new_null.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 549
if _libs["c-lightusd"].has("c_lightusd_value_free", "cdecl"):
    c_lightusd_value_free = _libs["c-lightusd"].get("c_lightusd_value_free", "cdecl")
    c_lightusd_value_free.argtypes = [POINTER(CLightUSDValue)]
    c_lightusd_value_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 556
if _libs["c-lightusd"].has("c_lightusd_value_to_string", "cdecl"):
    c_lightusd_value_to_string = _libs["c-lightusd"].get("c_lightusd_value_to_string", "cdecl")
    c_lightusd_value_to_string.argtypes = [POINTER(CLightUSDValue), POINTER(c_lightusd_string_t)]
    c_lightusd_value_to_string.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 563
if _libs["c-lightusd"].has("c_lightusd_value_free", "cdecl"):
    c_lightusd_value_free = _libs["c-lightusd"].get("c_lightusd_value_free", "cdecl")
    c_lightusd_value_free.argtypes = [POINTER(CLightUSDValue)]
    c_lightusd_value_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 570
if _libs["c-lightusd"].has("c_lightusd_value_new_token", "cdecl"):
    c_lightusd_value_new_token = _libs["c-lightusd"].get("c_lightusd_value_new_token", "cdecl")
    c_lightusd_value_new_token.argtypes = [POINTER(c_lightusd_token_t)]
    c_lightusd_value_new_token.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 578
if _libs["c-lightusd"].has("c_lightusd_value_new_string", "cdecl"):
    c_lightusd_value_new_string = _libs["c-lightusd"].get("c_lightusd_value_new_string", "cdecl")
    c_lightusd_value_new_string.argtypes = [POINTER(c_lightusd_string_t)]
    c_lightusd_value_new_string.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 586
if _libs["c-lightusd"].has("c_lightusd_value_new_int", "cdecl"):
    c_lightusd_value_new_int = _libs["c-lightusd"].get("c_lightusd_value_new_int", "cdecl")
    c_lightusd_value_new_int.argtypes = [c_int]
    c_lightusd_value_new_int.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 587
if _libs["c-lightusd"].has("c_lightusd_value_new_int2", "cdecl"):
    c_lightusd_value_new_int2 = _libs["c-lightusd"].get("c_lightusd_value_new_int2", "cdecl")
    c_lightusd_value_new_int2.argtypes = [c_lightusd_int2_t]
    c_lightusd_value_new_int2.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 588
if _libs["c-lightusd"].has("c_lightusd_value_new_int3", "cdecl"):
    c_lightusd_value_new_int3 = _libs["c-lightusd"].get("c_lightusd_value_new_int3", "cdecl")
    c_lightusd_value_new_int3.argtypes = [c_lightusd_int3_t]
    c_lightusd_value_new_int3.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 589
if _libs["c-lightusd"].has("c_lightusd_value_new_int4", "cdecl"):
    c_lightusd_value_new_int4 = _libs["c-lightusd"].get("c_lightusd_value_new_int4", "cdecl")
    c_lightusd_value_new_int4.argtypes = [c_lightusd_int4_t]
    c_lightusd_value_new_int4.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 590
if _libs["c-lightusd"].has("c_lightusd_value_new_float", "cdecl"):
    c_lightusd_value_new_float = _libs["c-lightusd"].get("c_lightusd_value_new_float", "cdecl")
    c_lightusd_value_new_float.argtypes = [c_float]
    c_lightusd_value_new_float.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 591
if _libs["c-lightusd"].has("c_lightusd_value_new_float2", "cdecl"):
    c_lightusd_value_new_float2 = _libs["c-lightusd"].get("c_lightusd_value_new_float2", "cdecl")
    c_lightusd_value_new_float2.argtypes = [c_lightusd_float2_t]
    c_lightusd_value_new_float2.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 593
if _libs["c-lightusd"].has("c_lightusd_value_new_float3", "cdecl"):
    c_lightusd_value_new_float3 = _libs["c-lightusd"].get("c_lightusd_value_new_float3", "cdecl")
    c_lightusd_value_new_float3.argtypes = [c_lightusd_float3_t]
    c_lightusd_value_new_float3.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 595
if _libs["c-lightusd"].has("c_lightusd_value_new_float4", "cdecl"):
    c_lightusd_value_new_float4 = _libs["c-lightusd"].get("c_lightusd_value_new_float4", "cdecl")
    c_lightusd_value_new_float4.argtypes = [c_lightusd_float4_t]
    c_lightusd_value_new_float4.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 603
if _libs["c-lightusd"].has("c_lightusd_value_new_array_int", "cdecl"):
    c_lightusd_value_new_array_int = _libs["c-lightusd"].get("c_lightusd_value_new_array_int", "cdecl")
    c_lightusd_value_new_array_int.argtypes = [uint64_t, POINTER(c_int)]
    c_lightusd_value_new_array_int.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 605
if _libs["c-lightusd"].has("c_lightusd_value_new_array_int2", "cdecl"):
    c_lightusd_value_new_array_int2 = _libs["c-lightusd"].get("c_lightusd_value_new_array_int2", "cdecl")
    c_lightusd_value_new_array_int2.argtypes = [uint64_t, POINTER(c_lightusd_int2_t)]
    c_lightusd_value_new_array_int2.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 607
if _libs["c-lightusd"].has("c_lightusd_value_new_array_int3", "cdecl"):
    c_lightusd_value_new_array_int3 = _libs["c-lightusd"].get("c_lightusd_value_new_array_int3", "cdecl")
    c_lightusd_value_new_array_int3.argtypes = [uint64_t, POINTER(c_lightusd_int3_t)]
    c_lightusd_value_new_array_int3.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 609
if _libs["c-lightusd"].has("c_lightusd_value_new_array_int4", "cdecl"):
    c_lightusd_value_new_array_int4 = _libs["c-lightusd"].get("c_lightusd_value_new_array_int4", "cdecl")
    c_lightusd_value_new_array_int4.argtypes = [uint64_t, POINTER(c_lightusd_int4_t)]
    c_lightusd_value_new_array_int4.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 611
if _libs["c-lightusd"].has("c_lightusd_value_new_array_float", "cdecl"):
    c_lightusd_value_new_array_float = _libs["c-lightusd"].get("c_lightusd_value_new_array_float", "cdecl")
    c_lightusd_value_new_array_float.argtypes = [uint64_t, POINTER(c_float)]
    c_lightusd_value_new_array_float.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 613
if _libs["c-lightusd"].has("c_lightusd_value_new_array_float2", "cdecl"):
    c_lightusd_value_new_array_float2 = _libs["c-lightusd"].get("c_lightusd_value_new_array_float2", "cdecl")
    c_lightusd_value_new_array_float2.argtypes = [uint64_t, POINTER(c_lightusd_float2_t)]
    c_lightusd_value_new_array_float2.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 615
if _libs["c-lightusd"].has("c_lightusd_value_new_array_float3", "cdecl"):
    c_lightusd_value_new_array_float3 = _libs["c-lightusd"].get("c_lightusd_value_new_array_float3", "cdecl")
    c_lightusd_value_new_array_float3.argtypes = [uint64_t, POINTER(c_lightusd_float3_t)]
    c_lightusd_value_new_array_float3.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 617
if _libs["c-lightusd"].has("c_lightusd_value_new_array_float4", "cdecl"):
    c_lightusd_value_new_array_float4 = _libs["c-lightusd"].get("c_lightusd_value_new_array_float4", "cdecl")
    c_lightusd_value_new_array_float4.argtypes = [uint64_t, POINTER(c_lightusd_float4_t)]
    c_lightusd_value_new_array_float4.restype = POINTER(CLightUSDValue)

# /mnt/data/work/lightusd/src/c-lightusd.h: 622
class struct_CLightUSDPath(Structure):
    pass

CLightUSDPath = struct_CLightUSDPath# /mnt/data/work/lightusd/src/c-lightusd.h: 622

# /mnt/data/work/lightusd/src/c-lightusd.h: 625
class struct_CLightUSDProperty(Structure):
    pass

CLightUSDProperty = struct_CLightUSDProperty# /mnt/data/work/lightusd/src/c-lightusd.h: 625

# /mnt/data/work/lightusd/src/c-lightusd.h: 628
class struct_CLightUSDRelationship(Structure):
    pass

CLightUSDRelationship = struct_CLightUSDRelationship# /mnt/data/work/lightusd/src/c-lightusd.h: 628

# /mnt/data/work/lightusd/src/c-lightusd.h: 630
for _lib in _libs.values():
    if not _lib.has("c_lightusd_relationsip_new", "cdecl"):
        continue
    c_lightusd_relationsip_new = _lib.get("c_lightusd_relationsip_new", "cdecl")
    c_lightusd_relationsip_new.argtypes = [uint32_t, POINTER(POINTER(c_char))]
    c_lightusd_relationsip_new.restype = POINTER(CLightUSDRelationship)
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 633
for _lib in _libs.values():
    if not _lib.has("c_lightusd_relationsip_free", "cdecl"):
        continue
    c_lightusd_relationsip_free = _lib.get("c_lightusd_relationsip_free", "cdecl")
    c_lightusd_relationsip_free.argtypes = [POINTER(CLightUSDRelationship)]
    c_lightusd_relationsip_free.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 635
for _lib in _libs.values():
    if not _lib.has("c_lightusd_relationsip_is_blocked", "cdecl"):
        continue
    c_lightusd_relationsip_is_blocked = _lib.get("c_lightusd_relationsip_is_blocked", "cdecl")
    c_lightusd_relationsip_is_blocked.argtypes = [POINTER(CLightUSDRelationship)]
    c_lightusd_relationsip_is_blocked.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 640
for _lib in _libs.values():
    if not _lib.has("c_lightusd_relationsip_num_targetPaths", "cdecl"):
        continue
    c_lightusd_relationsip_num_targetPaths = _lib.get("c_lightusd_relationsip_num_targetPaths", "cdecl")
    c_lightusd_relationsip_num_targetPaths.argtypes = [POINTER(CLightUSDRelationship)]
    c_lightusd_relationsip_num_targetPaths.restype = uint32_t
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 646
for _lib in _libs.values():
    if not _lib.has("c_lightusd_relationsip_get_targetPath", "cdecl"):
        continue
    c_lightusd_relationsip_get_targetPath = _lib.get("c_lightusd_relationsip_get_targetPath", "cdecl")
    c_lightusd_relationsip_get_targetPath.argtypes = [POINTER(CLightUSDRelationship), uint32_t, POINTER(CLightUSDPath)]
    c_lightusd_relationsip_get_targetPath.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 650
class struct_CLightUSDAttribute(Structure):
    pass

CLightUSDAttribute = struct_CLightUSDAttribute# /mnt/data/work/lightusd/src/c-lightusd.h: 650

# /mnt/data/work/lightusd/src/c-lightusd.h: 652
for _lib in _libs.values():
    if not _lib.has("c_lightusd_attribute_connection_set", "cdecl"):
        continue
    c_lightusd_attribute_connection_set = _lib.get("c_lightusd_attribute_connection_set", "cdecl")
    c_lightusd_attribute_connection_set.argtypes = [POINTER(CLightUSDAttribute), POINTER(CLightUSDPath)]
    c_lightusd_attribute_connection_set.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 655
for _lib in _libs.values():
    if not _lib.has("c_lightusd_attribute_connections_set", "cdecl"):
        continue
    c_lightusd_attribute_connections_set = _lib.get("c_lightusd_attribute_connections_set", "cdecl")
    c_lightusd_attribute_connections_set.argtypes = [POINTER(CLightUSDAttribute), uint32_t, POINTER(CLightUSDPath)]
    c_lightusd_attribute_connections_set.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 658
for _lib in _libs.values():
    if not _lib.has("c_lightusd_attribute_meta_set", "cdecl"):
        continue
    c_lightusd_attribute_meta_set = _lib.get("c_lightusd_attribute_meta_set", "cdecl")
    c_lightusd_attribute_meta_set.argtypes = [POINTER(CLightUSDAttribute), String, POINTER(CLightUSDValue)]
    c_lightusd_attribute_meta_set.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 667
for _lib in _libs.values():
    if not _lib.has("c_lightusd_attribute_meta_get", "cdecl"):
        continue
    c_lightusd_attribute_meta_get = _lib.get("c_lightusd_attribute_meta_get", "cdecl")
    c_lightusd_attribute_meta_get.argtypes = [POINTER(CLightUSDAttribute), String, POINTER(POINTER(CLightUSDValue))]
    c_lightusd_attribute_meta_get.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 676
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_new", "cdecl"):
        continue
    c_lightusd_property_new = _lib.get("c_lightusd_property_new", "cdecl")
    c_lightusd_property_new.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_new.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 677
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_new_attribute", "cdecl"):
        continue
    c_lightusd_property_new_attribute = _lib.get("c_lightusd_property_new_attribute", "cdecl")
    c_lightusd_property_new_attribute.argtypes = [POINTER(CLightUSDProperty), POINTER(CLightUSDAttribute)]
    c_lightusd_property_new_attribute.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 679
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_new_relationship", "cdecl"):
        continue
    c_lightusd_property_new_relationship = _lib.get("c_lightusd_property_new_relationship", "cdecl")
    c_lightusd_property_new_relationship.argtypes = [POINTER(CLightUSDProperty), POINTER(CLightUSDRelationship)]
    c_lightusd_property_new_relationship.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 681
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_free", "cdecl"):
        continue
    c_lightusd_property_free = _lib.get("c_lightusd_property_free", "cdecl")
    c_lightusd_property_free.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_free.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 683
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_set_attribute", "cdecl"):
        continue
    c_lightusd_property_set_attribute = _lib.get("c_lightusd_property_set_attribute", "cdecl")
    c_lightusd_property_set_attribute.argtypes = [POINTER(CLightUSDProperty), POINTER(CLightUSDAttribute)]
    c_lightusd_property_set_attribute.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 685
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_set_relationship", "cdecl"):
        continue
    c_lightusd_property_set_relationship = _lib.get("c_lightusd_property_set_relationship", "cdecl")
    c_lightusd_property_set_relationship.argtypes = [POINTER(CLightUSDProperty), POINTER(CLightUSDRelationship)]
    c_lightusd_property_set_relationship.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 688
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_is_attribute", "cdecl"):
        continue
    c_lightusd_property_is_attribute = _lib.get("c_lightusd_property_is_attribute", "cdecl")
    c_lightusd_property_is_attribute.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_is_attribute.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 689
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_is_attribute_connection", "cdecl"):
        continue
    c_lightusd_property_is_attribute_connection = _lib.get("c_lightusd_property_is_attribute_connection", "cdecl")
    c_lightusd_property_is_attribute_connection.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_is_attribute_connection.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 691
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_is_relationship", "cdecl"):
        continue
    c_lightusd_property_is_relationship = _lib.get("c_lightusd_property_is_relationship", "cdecl")
    c_lightusd_property_is_relationship.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_is_relationship.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 693
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_is_custom", "cdecl"):
        continue
    c_lightusd_property_is_custom = _lib.get("c_lightusd_property_is_custom", "cdecl")
    c_lightusd_property_is_custom.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_is_custom.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 694
for _lib in _libs.values():
    if not _lib.has("c_lightusd_property_is_varying", "cdecl"):
        continue
    c_lightusd_property_is_varying = _lib.get("c_lightusd_property_is_varying", "cdecl")
    c_lightusd_property_is_varying.argtypes = [POINTER(CLightUSDProperty)]
    c_lightusd_property_is_varying.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 696
class struct_CLightUSDPrim(Structure):
    pass

CLightUSDPrim = struct_CLightUSDPrim# /mnt/data/work/lightusd/src/c-lightusd.h: 696

# /mnt/data/work/lightusd/src/c-lightusd.h: 708
if _libs["c-lightusd"].has("c_lightusd_prim_new", "cdecl"):
    c_lightusd_prim_new = _libs["c-lightusd"].get("c_lightusd_prim_new", "cdecl")
    c_lightusd_prim_new.argtypes = [String, POINTER(c_lightusd_string_t)]
    c_lightusd_prim_new.restype = POINTER(CLightUSDPrim)

# /mnt/data/work/lightusd/src/c-lightusd.h: 715
if _libs["c-lightusd"].has("c_lightusd_prim_new_builtin", "cdecl"):
    c_lightusd_prim_new_builtin = _libs["c-lightusd"].get("c_lightusd_prim_new_builtin", "cdecl")
    c_lightusd_prim_new_builtin.argtypes = [CLightUSDPrimType]
    c_lightusd_prim_new_builtin.restype = POINTER(CLightUSDPrim)

# /mnt/data/work/lightusd/src/c-lightusd.h: 717
if _libs["c-lightusd"].has("c_lightusd_prim_to_string", "cdecl"):
    c_lightusd_prim_to_string = _libs["c-lightusd"].get("c_lightusd_prim_to_string", "cdecl")
    c_lightusd_prim_to_string.argtypes = [POINTER(CLightUSDPrim), POINTER(c_lightusd_string_t)]
    c_lightusd_prim_to_string.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 720
if _libs["c-lightusd"].has("c_lightusd_prim_free", "cdecl"):
    c_lightusd_prim_free = _libs["c-lightusd"].get("c_lightusd_prim_free", "cdecl")
    c_lightusd_prim_free.argtypes = [POINTER(CLightUSDPrim)]
    c_lightusd_prim_free.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 724
if _libs["c-lightusd"].has("c_lightusd_prim_type", "cdecl"):
    c_lightusd_prim_type = _libs["c-lightusd"].get("c_lightusd_prim_type", "cdecl")
    c_lightusd_prim_type.argtypes = [POINTER(CLightUSDPrim)]
    c_lightusd_prim_type.restype = c_char_p

# /mnt/data/work/lightusd/src/c-lightusd.h: 731
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_element_name", "cdecl"):
        continue
    c_lightusd_prim_element_name = _lib.get("c_lightusd_prim_element_name", "cdecl")
    c_lightusd_prim_element_name.argtypes = [POINTER(CLightUSDPrim)]
    c_lightusd_prim_element_name.restype = c_char_p
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 744
if _libs["c-lightusd"].has("c_lightusd_prim_get_property_names", "cdecl"):
    c_lightusd_prim_get_property_names = _libs["c-lightusd"].get("c_lightusd_prim_get_property_names", "cdecl")
    c_lightusd_prim_get_property_names.argtypes = [POINTER(CLightUSDPrim), POINTER(c_lightusd_token_vector_t)]
    c_lightusd_prim_get_property_names.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 752
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_property_get", "cdecl"):
        continue
    c_lightusd_prim_property_get = _lib.get("c_lightusd_prim_property_get", "cdecl")
    c_lightusd_prim_property_get.argtypes = [POINTER(CLightUSDPrim), String, POINTER(CLightUSDProperty)]
    c_lightusd_prim_property_get.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 762
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_property_add", "cdecl"):
        continue
    c_lightusd_prim_property_add = _lib.get("c_lightusd_prim_property_add", "cdecl")
    c_lightusd_prim_property_add.argtypes = [POINTER(CLightUSDPrim), String, POINTER(CLightUSDProperty), POINTER(c_lightusd_string_t)]
    c_lightusd_prim_property_add.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 771
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_property_del", "cdecl"):
        continue
    c_lightusd_prim_property_del = _lib.get("c_lightusd_prim_property_del", "cdecl")
    c_lightusd_prim_property_del.argtypes = [POINTER(CLightUSDPrim), String]
    c_lightusd_prim_property_del.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 778
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_meta_set", "cdecl"):
        continue
    c_lightusd_prim_meta_set = _lib.get("c_lightusd_prim_meta_set", "cdecl")
    c_lightusd_prim_meta_set.argtypes = [POINTER(CLightUSDPrim), String, POINTER(CLightUSDValue)]
    c_lightusd_prim_meta_set.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 787
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_meta_get", "cdecl"):
        continue
    c_lightusd_prim_meta_get = _lib.get("c_lightusd_prim_meta_get", "cdecl")
    c_lightusd_prim_meta_get.argtypes = [POINTER(CLightUSDPrim), String, POINTER(POINTER(CLightUSDValue))]
    c_lightusd_prim_meta_get.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 795
for _lib in _libs.values():
    if not _lib.has("c_lightusd_prim_meta_authored", "cdecl"):
        continue
    c_lightusd_prim_meta_authored = _lib.get("c_lightusd_prim_meta_authored", "cdecl")
    c_lightusd_prim_meta_authored.argtypes = [POINTER(CLightUSDPrim), String]
    c_lightusd_prim_meta_authored.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 803
if _libs["c-lightusd"].has("c_lightusd_prim_append_child", "cdecl"):
    c_lightusd_prim_append_child = _libs["c-lightusd"].get("c_lightusd_prim_append_child", "cdecl")
    c_lightusd_prim_append_child.argtypes = [POINTER(CLightUSDPrim), POINTER(CLightUSDPrim)]
    c_lightusd_prim_append_child.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 812
if _libs["c-lightusd"].has("c_lightusd_prim_append_child_move", "cdecl"):
    c_lightusd_prim_append_child_move = _libs["c-lightusd"].get("c_lightusd_prim_append_child_move", "cdecl")
    c_lightusd_prim_append_child_move.argtypes = [POINTER(CLightUSDPrim), POINTER(CLightUSDPrim)]
    c_lightusd_prim_append_child_move.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 819
if _libs["c-lightusd"].has("c_lightusd_prim_del_child", "cdecl"):
    c_lightusd_prim_del_child = _libs["c-lightusd"].get("c_lightusd_prim_del_child", "cdecl")
    c_lightusd_prim_del_child.argtypes = [POINTER(CLightUSDPrim), uint64_t]
    c_lightusd_prim_del_child.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 827
if _libs["c-lightusd"].has("c_lightusd_prim_num_children", "cdecl"):
    c_lightusd_prim_num_children = _libs["c-lightusd"].get("c_lightusd_prim_num_children", "cdecl")
    c_lightusd_prim_num_children.argtypes = [POINTER(CLightUSDPrim)]
    c_lightusd_prim_num_children.restype = uint64_t

# /mnt/data/work/lightusd/src/c-lightusd.h: 841
if _libs["c-lightusd"].has("c_lightusd_prim_get_child", "cdecl"):
    c_lightusd_prim_get_child = _libs["c-lightusd"].get("c_lightusd_prim_get_child", "cdecl")
    c_lightusd_prim_get_child.argtypes = [POINTER(CLightUSDPrim), uint64_t, POINTER(POINTER(CLightUSDPrim))]
    c_lightusd_prim_get_child.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 846
class struct_CLightUSDStage(Structure):
    pass

CLightUSDStage = struct_CLightUSDStage# /mnt/data/work/lightusd/src/c-lightusd.h: 846

# /mnt/data/work/lightusd/src/c-lightusd.h: 848
if _libs["c-lightusd"].has("c_lightusd_stage_new", "cdecl"):
    c_lightusd_stage_new = _libs["c-lightusd"].get("c_lightusd_stage_new", "cdecl")
    c_lightusd_stage_new.argtypes = []
    c_lightusd_stage_new.restype = POINTER(CLightUSDStage)

# /mnt/data/work/lightusd/src/c-lightusd.h: 849
if _libs["c-lightusd"].has("c_lightusd_stage_to_string", "cdecl"):
    c_lightusd_stage_to_string = _libs["c-lightusd"].get("c_lightusd_stage_to_string", "cdecl")
    c_lightusd_stage_to_string.argtypes = [POINTER(CLightUSDStage), POINTER(c_lightusd_string_t)]
    c_lightusd_stage_to_string.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 851
if _libs["c-lightusd"].has("c_lightusd_stage_free", "cdecl"):
    c_lightusd_stage_free = _libs["c-lightusd"].get("c_lightusd_stage_free", "cdecl")
    c_lightusd_stage_free.argtypes = [POINTER(CLightUSDStage)]
    c_lightusd_stage_free.restype = c_int

CLightUSDTraversalFunction = CFUNCTYPE(UNCHECKED(c_int), POINTER(CLightUSDPrim), POINTER(CLightUSDPath))# /mnt/data/work/lightusd/src/c-lightusd.h: 857

# /mnt/data/work/lightusd/src/c-lightusd.h: 873
if _libs["c-lightusd"].has("c_lightusd_stage_traverse", "cdecl"):
    c_lightusd_stage_traverse = _libs["c-lightusd"].get("c_lightusd_stage_traverse", "cdecl")
    c_lightusd_stage_traverse.argtypes = [POINTER(CLightUSDStage), CLightUSDTraversalFunction, POINTER(c_lightusd_string_t)]
    c_lightusd_stage_traverse.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 880
if _libs["c-lightusd"].has("c_lightusd_detect_format", "cdecl"):
    c_lightusd_detect_format = _libs["c-lightusd"].get("c_lightusd_detect_format", "cdecl")
    c_lightusd_detect_format.argtypes = [String]
    c_lightusd_detect_format.restype = CLightUSDFormat

# /mnt/data/work/lightusd/src/c-lightusd.h: 882
if _libs["c-lightusd"].has("c_lightusd_is_usd_file", "cdecl"):
    c_lightusd_is_usd_file = _libs["c-lightusd"].get("c_lightusd_is_usd_file", "cdecl")
    c_lightusd_is_usd_file.argtypes = [String]
    c_lightusd_is_usd_file.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 883
if _libs["c-lightusd"].has("c_lightusd_is_usda_file", "cdecl"):
    c_lightusd_is_usda_file = _libs["c-lightusd"].get("c_lightusd_is_usda_file", "cdecl")
    c_lightusd_is_usda_file.argtypes = [String]
    c_lightusd_is_usda_file.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 884
if _libs["c-lightusd"].has("c_lightusd_is_usdc_file", "cdecl"):
    c_lightusd_is_usdc_file = _libs["c-lightusd"].get("c_lightusd_is_usdc_file", "cdecl")
    c_lightusd_is_usdc_file.argtypes = [String]
    c_lightusd_is_usdc_file.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 885
if _libs["c-lightusd"].has("c_lightusd_is_usdz_file", "cdecl"):
    c_lightusd_is_usdz_file = _libs["c-lightusd"].get("c_lightusd_is_usdz_file", "cdecl")
    c_lightusd_is_usdz_file.argtypes = [String]
    c_lightusd_is_usdz_file.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 890
for _lib in _libs.values():
    if not _lib.has("c_lightusd_detect_format_w", "cdecl"):
        continue
    c_lightusd_detect_format_w = _lib.get("c_lightusd_detect_format_w", "cdecl")
    c_lightusd_detect_format_w.argtypes = [POINTER(c_wchar)]
    c_lightusd_detect_format_w.restype = CLightUSDFormat
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 892
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usd_file_w", "cdecl"):
        continue
    c_lightusd_is_usd_file_w = _lib.get("c_lightusd_is_usd_file_w", "cdecl")
    c_lightusd_is_usd_file_w.argtypes = [POINTER(c_wchar)]
    c_lightusd_is_usd_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 893
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usda_file_w", "cdecl"):
        continue
    c_lightusd_is_usda_file_w = _lib.get("c_lightusd_is_usda_file_w", "cdecl")
    c_lightusd_is_usda_file_w.argtypes = [POINTER(c_wchar)]
    c_lightusd_is_usda_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 894
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usdc_file_w", "cdecl"):
        continue
    c_lightusd_is_usdc_file_w = _lib.get("c_lightusd_is_usdc_file_w", "cdecl")
    c_lightusd_is_usdc_file_w.argtypes = [POINTER(c_wchar)]
    c_lightusd_is_usdc_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 895
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usdz_file_w", "cdecl"):
        continue
    c_lightusd_is_usdz_file_w = _lib.get("c_lightusd_is_usdz_file_w", "cdecl")
    c_lightusd_is_usdz_file_w.argtypes = [POINTER(c_wchar)]
    c_lightusd_is_usdz_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 897
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usd_memory", "cdecl"):
        continue
    c_lightusd_is_usd_memory = _lib.get("c_lightusd_is_usd_memory", "cdecl")
    c_lightusd_is_usd_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_lightusd_is_usd_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 899
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usda_memory", "cdecl"):
        continue
    c_lightusd_is_usda_memory = _lib.get("c_lightusd_is_usda_memory", "cdecl")
    c_lightusd_is_usda_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_lightusd_is_usda_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 901
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usdc_memory", "cdecl"):
        continue
    c_lightusd_is_usdc_memory = _lib.get("c_lightusd_is_usdc_memory", "cdecl")
    c_lightusd_is_usdc_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_lightusd_is_usdc_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 903
for _lib in _libs.values():
    if not _lib.has("c_lightusd_is_usdz_memory", "cdecl"):
        continue
    c_lightusd_is_usdz_memory = _lib.get("c_lightusd_is_usdz_memory", "cdecl")
    c_lightusd_is_usdz_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_lightusd_is_usdz_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 909
if _libs["c-lightusd"].has("c_lightusd_load_usd_from_file", "cdecl"):
    c_lightusd_load_usd_from_file = _libs["c-lightusd"].get("c_lightusd_load_usd_from_file", "cdecl")
    c_lightusd_load_usd_from_file.argtypes = [String, POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usd_from_file.restype = c_int

# /mnt/data/work/lightusd/src/c-lightusd.h: 913
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usda_from_file", "cdecl"):
        continue
    c_lightusd_load_usda_from_file = _lib.get("c_lightusd_load_usda_from_file", "cdecl")
    c_lightusd_load_usda_from_file.argtypes = [String, POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usda_from_file.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 917
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usdc_from_file", "cdecl"):
        continue
    c_lightusd_load_usdc_from_file = _lib.get("c_lightusd_load_usdc_from_file", "cdecl")
    c_lightusd_load_usdc_from_file.argtypes = [String, POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usdc_from_file.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 921
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usdz_from_file", "cdecl"):
        continue
    c_lightusd_load_usdz_from_file = _lib.get("c_lightusd_load_usdz_from_file", "cdecl")
    c_lightusd_load_usdz_from_file.argtypes = [String, POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usdz_from_file.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 929
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usd_from_file_w", "cdecl"):
        continue
    c_lightusd_load_usd_from_file_w = _lib.get("c_lightusd_load_usd_from_file_w", "cdecl")
    c_lightusd_load_usd_from_file_w.argtypes = [POINTER(c_wchar), POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usd_from_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 933
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usda_from_file_w", "cdecl"):
        continue
    c_lightusd_load_usda_from_file_w = _lib.get("c_lightusd_load_usda_from_file_w", "cdecl")
    c_lightusd_load_usda_from_file_w.argtypes = [POINTER(c_wchar), POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usda_from_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 937
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usdc_from_file_w", "cdecl"):
        continue
    c_lightusd_load_usdc_from_file_w = _lib.get("c_lightusd_load_usdc_from_file_w", "cdecl")
    c_lightusd_load_usdc_from_file_w.argtypes = [POINTER(c_wchar), POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usdc_from_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 941
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usdz_from_file_w", "cdecl"):
        continue
    c_lightusd_load_usdz_from_file_w = _lib.get("c_lightusd_load_usdz_from_file_w", "cdecl")
    c_lightusd_load_usdz_from_file_w.argtypes = [POINTER(c_wchar), POINTER(CLightUSDStage), POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usdz_from_file_w.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 946
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usd_from_memory", "cdecl"):
        continue
    c_lightusd_load_usd_from_memory = _lib.get("c_lightusd_load_usd_from_memory", "cdecl")
    c_lightusd_load_usd_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usd_from_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 950
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usda_from_memory", "cdecl"):
        continue
    c_lightusd_load_usda_from_memory = _lib.get("c_lightusd_load_usda_from_memory", "cdecl")
    c_lightusd_load_usda_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usda_from_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 954
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usdc_from_memory", "cdecl"):
        continue
    c_lightusd_load_usdc_from_memory = _lib.get("c_lightusd_load_usdc_from_memory", "cdecl")
    c_lightusd_load_usdc_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usdc_from_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 958
for _lib in _libs.values():
    if not _lib.has("c_lightusd_load_usdz_from_memory", "cdecl"):
        continue
    c_lightusd_load_usdz_from_memory = _lib.get("c_lightusd_load_usdz_from_memory", "cdecl")
    c_lightusd_load_usdz_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_lightusd_string_t), POINTER(c_lightusd_string_t)]
    c_lightusd_load_usdz_from_memory.restype = c_int
    break

# /mnt/data/work/lightusd/src/c-lightusd.h: 105
try:
    C_LIGHTUSD_MAX_DIM = 1
except:
    pass

# /mnt/data/work/lightusd/src/c-lightusd.h: 192
try:
    C_LIGHTUSD_VALUE_1D_BIT = (1 << 10)
except:
    pass

c_lightusd_token_t = struct_c_lightusd_token_t# /mnt/data/work/lightusd/src/c-lightusd.h: 369

c_lightusd_token_vector_t = struct_c_lightusd_token_vector_t# /mnt/data/work/lightusd/src/c-lightusd.h: 393

c_lightusd_string_t = struct_c_lightusd_string_t# /mnt/data/work/lightusd/src/c-lightusd.h: 431

CLightUSDValue = struct_CLightUSDValue# /mnt/data/work/lightusd/src/c-lightusd.h: 537

CLightUSDPath = struct_CLightUSDPath# /mnt/data/work/lightusd/src/c-lightusd.h: 622

CLightUSDProperty = struct_CLightUSDProperty# /mnt/data/work/lightusd/src/c-lightusd.h: 625

CLightUSDRelationship = struct_CLightUSDRelationship# /mnt/data/work/lightusd/src/c-lightusd.h: 628

CLightUSDAttribute = struct_CLightUSDAttribute# /mnt/data/work/lightusd/src/c-lightusd.h: 650

CLightUSDPrim = struct_CLightUSDPrim# /mnt/data/work/lightusd/src/c-lightusd.h: 696

CLightUSDStage = struct_CLightUSDStage# /mnt/data/work/lightusd/src/c-lightusd.h: 846

# No inserted files

# No prefix-stripping
