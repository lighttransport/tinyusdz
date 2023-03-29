r"""Wrapper for c-tinyusd.h

Generated with:
/home/syoyo/miniconda3/bin/ctypesgen -lc-tinyusd ../../src/c-tinyusd.h -o ctinyusd.py

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
_libs["c-tinyusd"] = load_library("c-tinyusd")

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

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 44
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_malloc", "cdecl"):
        continue
    c_tinyusd_malloc = _lib.get("c_tinyusd_malloc", "cdecl")
    c_tinyusd_malloc.argtypes = [c_size_t]
    c_tinyusd_malloc.restype = POINTER(c_ubyte)
    c_tinyusd_malloc.errcheck = lambda v,*a : cast(v, c_void_p)
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 47
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_free", "cdecl"):
        continue
    c_tinyusd_free = _lib.get("c_tinyusd_free", "cdecl")
    c_tinyusd_free.argtypes = [POINTER(None)]
    c_tinyusd_free.restype = c_int
    break

enum_anon_4 = c_int# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

C_TINYUSD_FORMAT_UNKNOWN = 0# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

C_TINYUSD_FORMAT_AUTO = (C_TINYUSD_FORMAT_UNKNOWN + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

C_TINYUSD_FORMAT_USDA = (C_TINYUSD_FORMAT_AUTO + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

C_TINYUSD_FORMAT_USDC = (C_TINYUSD_FORMAT_USDA + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

C_TINYUSD_FORMAT_USDZ = (C_TINYUSD_FORMAT_USDC + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

CTinyUSDFormat = enum_anon_4# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 60

enum_anon_5 = c_int# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 67

C_TINYUSD_AXIS_UNKNOWN = 0# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 67

C_TINYUSD_AXIS_X = (C_TINYUSD_AXIS_UNKNOWN + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 67

C_TINYUSD_AXIS_Y = (C_TINYUSD_AXIS_X + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 67

C_TINYUSD_AXIS_Z = (C_TINYUSD_AXIS_Y + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 67

CTinyUSDAxis = enum_anon_5# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 67

enum_anon_6 = c_int# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TOKEN = 0# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TOKEN_VECTOR = (C_TINYUSD_VALUE_TOKEN + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_STRING = (C_TINYUSD_VALUE_TOKEN_VECTOR + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_STRING_VECTOR = (C_TINYUSD_VALUE_STRING + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_BOOL = (C_TINYUSD_VALUE_STRING_VECTOR + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_HALF = (C_TINYUSD_VALUE_BOOL + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_HALF2 = (C_TINYUSD_VALUE_HALF + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_HALF3 = (C_TINYUSD_VALUE_HALF2 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_HALF4 = (C_TINYUSD_VALUE_HALF3 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_INT = (C_TINYUSD_VALUE_HALF4 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_INT2 = (C_TINYUSD_VALUE_INT + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_INT3 = (C_TINYUSD_VALUE_INT2 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_INT4 = (C_TINYUSD_VALUE_INT3 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_UINT = (C_TINYUSD_VALUE_INT4 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_UINT2 = (C_TINYUSD_VALUE_UINT + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_UINT3 = (C_TINYUSD_VALUE_UINT2 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_UINT4 = (C_TINYUSD_VALUE_UINT3 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_INT64 = (C_TINYUSD_VALUE_UINT4 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_UINT64 = (C_TINYUSD_VALUE_INT64 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_FLOAT = (C_TINYUSD_VALUE_UINT64 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_FLOAT2 = (C_TINYUSD_VALUE_FLOAT + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_FLOAT3 = (C_TINYUSD_VALUE_FLOAT2 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_FLOAT4 = (C_TINYUSD_VALUE_FLOAT3 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_DOUBLE = (C_TINYUSD_VALUE_FLOAT4 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_DOUBLE2 = (C_TINYUSD_VALUE_DOUBLE + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_DOUBLE3 = (C_TINYUSD_VALUE_DOUBLE2 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_DOUBLE4 = (C_TINYUSD_VALUE_DOUBLE3 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_QUATH = (C_TINYUSD_VALUE_DOUBLE4 + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_QUATF = (C_TINYUSD_VALUE_QUATH + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_QUATD = (C_TINYUSD_VALUE_QUATF + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_COLOR3H = (C_TINYUSD_VALUE_QUATD + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_COLOR3F = (C_TINYUSD_VALUE_COLOR3H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_COLOR3D = (C_TINYUSD_VALUE_COLOR3F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_COLOR4H = (C_TINYUSD_VALUE_COLOR3D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_COLOR4F = (C_TINYUSD_VALUE_COLOR4H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_COLOR4D = (C_TINYUSD_VALUE_COLOR4F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TEXCOORD2H = (C_TINYUSD_VALUE_COLOR4D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TEXCOORD2F = (C_TINYUSD_VALUE_TEXCOORD2H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TEXCOORD2D = (C_TINYUSD_VALUE_TEXCOORD2F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TEXCOORD3H = (C_TINYUSD_VALUE_TEXCOORD2D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TEXCOORD3F = (C_TINYUSD_VALUE_TEXCOORD3H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_TEXCOORD3D = (C_TINYUSD_VALUE_TEXCOORD3F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_NORMAL3H = (C_TINYUSD_VALUE_TEXCOORD3D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_NORMAL3F = (C_TINYUSD_VALUE_NORMAL3H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_NORMAL3D = (C_TINYUSD_VALUE_NORMAL3F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_VECTOR3H = (C_TINYUSD_VALUE_NORMAL3D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_VECTOR3F = (C_TINYUSD_VALUE_VECTOR3H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_VECTOR3D = (C_TINYUSD_VALUE_VECTOR3F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_POINT3H = (C_TINYUSD_VALUE_VECTOR3D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_POINT3F = (C_TINYUSD_VALUE_POINT3H + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_POINT3D = (C_TINYUSD_VALUE_POINT3F + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_MATRIX2D = (C_TINYUSD_VALUE_POINT3D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_MATRIX3D = (C_TINYUSD_VALUE_MATRIX2D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_MATRIX4D = (C_TINYUSD_VALUE_MATRIX3D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_FRAME4D = (C_TINYUSD_VALUE_MATRIX4D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

C_TINYUSD_VALUE_END = (C_TINYUSD_VALUE_FRAME4D + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

CTinyUSDValueType = enum_anon_6# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 134

enum_anon_7 = c_int# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_UNKNOWN = 0# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_MODEL = (C_TINYUSD_PRIM_UNKNOWN + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_XFORM = (C_TINYUSD_PRIM_MODEL + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_MESH = (C_TINYUSD_PRIM_XFORM + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_GEOMSUBSET = (C_TINYUSD_PRIM_MESH + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_MATERIAL = (C_TINYUSD_PRIM_GEOMSUBSET + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_SHADER = (C_TINYUSD_PRIM_MATERIAL + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_CAMERA = (C_TINYUSD_PRIM_SHADER + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_SPHERE_LIGHT = (C_TINYUSD_PRIM_CAMERA + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_DISTANT_LIGHT = (C_TINYUSD_PRIM_SPHERE_LIGHT + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_RECT_LIGHT = (C_TINYUSD_PRIM_DISTANT_LIGHT + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

C_TINYUSD_PRIM_END = (C_TINYUSD_PRIM_RECT_LIGHT + 1)# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

CTinyUSDPrimType = enum_anon_7# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 154

c_tinyusd_half = uint16_t# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 160

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 167
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

c_tinyusd_int2 = struct_anon_8# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 167

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 174
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

c_tinyusd_int3 = struct_anon_9# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 174

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 182
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

c_tinyusd_int4 = struct_anon_10# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 182

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 188
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

c_tinyusd_uint2 = struct_anon_11# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 188

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 194
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

c_tinyusd_uint3 = struct_anon_12# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 194

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 201
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

c_tinyusd_uint4 = struct_anon_13# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 201

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 206
class struct_anon_14(Structure):
    pass

struct_anon_14.__slots__ = [
    'x',
    'y',
]
struct_anon_14._fields_ = [
    ('x', c_tinyusd_half),
    ('y', c_tinyusd_half),
]

c_tinyusd_half2 = struct_anon_14# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 206

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 213
class struct_anon_15(Structure):
    pass

struct_anon_15.__slots__ = [
    'x',
    'y',
    'z',
]
struct_anon_15._fields_ = [
    ('x', c_tinyusd_half),
    ('y', c_tinyusd_half),
    ('z', c_tinyusd_half),
]

c_tinyusd_half3 = struct_anon_15# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 213

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 221
class struct_anon_16(Structure):
    pass

struct_anon_16.__slots__ = [
    'x',
    'y',
    'z',
    'w',
]
struct_anon_16._fields_ = [
    ('x', c_tinyusd_half),
    ('y', c_tinyusd_half),
    ('z', c_tinyusd_half),
    ('w', c_tinyusd_half),
]

c_tinyusd_half4 = struct_anon_16# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 221

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 227
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

c_tinyusd_float2 = struct_anon_17# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 227

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 233
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

c_tinyusd_float3 = struct_anon_18# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 233

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 240
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

c_tinyusd_float4 = struct_anon_19# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 240

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 245
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

c_tinyusd_double2 = struct_anon_20# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 245

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 251
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

c_tinyusd_double3 = struct_anon_21# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 251

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 258
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

c_tinyusd_double4 = struct_anon_22# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 258

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 262
class struct_anon_23(Structure):
    pass

struct_anon_23.__slots__ = [
    'm',
]
struct_anon_23._fields_ = [
    ('m', c_double * int(4)),
]

c_tinyusd_matrix2d = struct_anon_23# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 262

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 266
class struct_anon_24(Structure):
    pass

struct_anon_24.__slots__ = [
    'm',
]
struct_anon_24._fields_ = [
    ('m', c_double * int(9)),
]

c_tinyusd_matrix3d = struct_anon_24# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 266

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 270
class struct_anon_25(Structure):
    pass

struct_anon_25.__slots__ = [
    'm',
]
struct_anon_25._fields_ = [
    ('m', c_double * int(16)),
]

c_tinyusd_matrix4d = struct_anon_25# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 270

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 275
class struct_anon_26(Structure):
    pass

struct_anon_26.__slots__ = [
    'imag',
    'real',
]
struct_anon_26._fields_ = [
    ('imag', c_tinyusd_half * int(3)),
    ('real', c_tinyusd_half),
]

c_tinyusd_quath = struct_anon_26# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 275

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 281
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

c_tinyusd_quatf = struct_anon_27# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 281

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 287
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

c_tinyusd_quatd = struct_anon_28# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 287

c_tinyusd_color3h = c_tinyusd_half3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 290

c_tinyusd_color3f = c_tinyusd_float3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 291

c_tinyusd_color3d = c_tinyusd_double3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 292

c_tinyusd_color4h = c_tinyusd_half4# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 294

c_tinyusd_color4f = c_tinyusd_float4# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 295

c_tinyusd_color4d = c_tinyusd_double4# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 296

c_tinyusd_point3h = c_tinyusd_half3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 298

c_tinyusd_point3f = c_tinyusd_float3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 299

c_tinyusd_point3d = c_tinyusd_double3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 300

c_tinyusd_normal3h = c_tinyusd_half3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 302

c_tinyusd_normal3f = c_tinyusd_float3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 303

c_tinyusd_normal3d = c_tinyusd_double3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 304

c_tinyusd_vector3h = c_tinyusd_half3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 306

c_tinyusd_vector3f = c_tinyusd_float3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 307

c_tinyusd_vector3d = c_tinyusd_double3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 308

c_tinyusd_frame4d = c_tinyusd_matrix4d# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 310

c_tinyusd_texcoord2h = c_tinyusd_half2# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 312

c_tinyusd_texcoord2f = c_tinyusd_float2# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 313

c_tinyusd_texcoord2d = c_tinyusd_double2# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 314

c_tinyusd_texcoord3h = c_tinyusd_half3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 316

c_tinyusd_texcoord3f = c_tinyusd_float3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 317

c_tinyusd_texcoord3d = c_tinyusd_double3# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 318

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 322
class struct_anon_29(Structure):
    pass

struct_anon_29.__slots__ = [
    'data',
]
struct_anon_29._fields_ = [
    ('data', POINTER(None)),
]

c_tinyusd_token = struct_anon_29# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 322

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 326
if _libs["c-tinyusd"].has("c_tinyusd_token_init", "cdecl"):
    c_tinyusd_token_init = _libs["c-tinyusd"].get("c_tinyusd_token_init", "cdecl")
    c_tinyusd_token_init.argtypes = []
    c_tinyusd_token_init.restype = c_tinyusd_token

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 330
if _libs["c-tinyusd"].has("c_tinyusd_token_new", "cdecl"):
    c_tinyusd_token_new = _libs["c-tinyusd"].get("c_tinyusd_token_new", "cdecl")
    c_tinyusd_token_new.argtypes = [POINTER(c_tinyusd_token), String]
    c_tinyusd_token_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 335
if _libs["c-tinyusd"].has("c_tinyusd_token_dup", "cdecl"):
    c_tinyusd_token_dup = _libs["c-tinyusd"].get("c_tinyusd_token_dup", "cdecl")
    c_tinyusd_token_dup.argtypes = [POINTER(c_tinyusd_token)]
    c_tinyusd_token_dup.restype = c_tinyusd_token

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 338
if _libs["c-tinyusd"].has("c_tinyusd_token_size", "cdecl"):
    c_tinyusd_token_size = _libs["c-tinyusd"].get("c_tinyusd_token_size", "cdecl")
    c_tinyusd_token_size.argtypes = [POINTER(c_tinyusd_token)]
    c_tinyusd_token_size.restype = c_size_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 342
if _libs["c-tinyusd"].has("c_tinyusd_token_str", "cdecl"):
    c_tinyusd_token_str = _libs["c-tinyusd"].get("c_tinyusd_token_str", "cdecl")
    c_tinyusd_token_str.argtypes = [POINTER(c_tinyusd_token)]
    c_tinyusd_token_str.restype = c_char_p

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 346
if _libs["c-tinyusd"].has("c_tinyusd_token_free", "cdecl"):
    c_tinyusd_token_free = _libs["c-tinyusd"].get("c_tinyusd_token_free", "cdecl")
    c_tinyusd_token_free.argtypes = [POINTER(c_tinyusd_token)]
    c_tinyusd_token_free.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 350
class struct_anon_30(Structure):
    pass

struct_anon_30.__slots__ = [
    'data',
]
struct_anon_30._fields_ = [
    ('data', POINTER(None)),
]

c_tinyusd_token_vector = struct_anon_30# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 350

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 355
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_new_empty", "cdecl"):
    c_tinyusd_token_vector_new_empty = _libs["c-tinyusd"].get("c_tinyusd_token_vector_new_empty", "cdecl")
    c_tinyusd_token_vector_new_empty.argtypes = [POINTER(c_tinyusd_token_vector)]
    c_tinyusd_token_vector_new_empty.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 360
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_new", "cdecl"):
    c_tinyusd_token_vector_new = _libs["c-tinyusd"].get("c_tinyusd_token_vector_new", "cdecl")
    c_tinyusd_token_vector_new.argtypes = [POINTER(c_tinyusd_token_vector), c_size_t, POINTER(POINTER(c_char))]
    c_tinyusd_token_vector_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 361
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_free", "cdecl"):
    c_tinyusd_token_vector_free = _libs["c-tinyusd"].get("c_tinyusd_token_vector_free", "cdecl")
    c_tinyusd_token_vector_free.argtypes = [POINTER(c_tinyusd_token_vector)]
    c_tinyusd_token_vector_free.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 367
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_size", "cdecl"):
    c_tinyusd_token_vector_size = _libs["c-tinyusd"].get("c_tinyusd_token_vector_size", "cdecl")
    c_tinyusd_token_vector_size.argtypes = [POINTER(c_tinyusd_token_vector)]
    c_tinyusd_token_vector_size.restype = c_size_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 369
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_clear", "cdecl"):
    c_tinyusd_token_vector_clear = _libs["c-tinyusd"].get("c_tinyusd_token_vector_clear", "cdecl")
    c_tinyusd_token_vector_clear.argtypes = [POINTER(c_tinyusd_token_vector)]
    c_tinyusd_token_vector_clear.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 370
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_resize", "cdecl"):
    c_tinyusd_token_vector_resize = _libs["c-tinyusd"].get("c_tinyusd_token_vector_resize", "cdecl")
    c_tinyusd_token_vector_resize.argtypes = [POINTER(c_tinyusd_token_vector), c_size_t]
    c_tinyusd_token_vector_resize.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 376
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_str", "cdecl"):
    c_tinyusd_token_vector_str = _libs["c-tinyusd"].get("c_tinyusd_token_vector_str", "cdecl")
    c_tinyusd_token_vector_str.argtypes = [POINTER(c_tinyusd_token_vector), c_size_t]
    c_tinyusd_token_vector_str.restype = c_char_p

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 382
if _libs["c-tinyusd"].has("c_tinyusd_token_vector_replace", "cdecl"):
    c_tinyusd_token_vector_replace = _libs["c-tinyusd"].get("c_tinyusd_token_vector_replace", "cdecl")
    c_tinyusd_token_vector_replace.argtypes = [POINTER(c_tinyusd_token_vector), c_size_t, String]
    c_tinyusd_token_vector_replace.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 387
class struct_anon_31(Structure):
    pass

struct_anon_31.__slots__ = [
    'data',
]
struct_anon_31._fields_ = [
    ('data', POINTER(None)),
]

c_tinyusd_string = struct_anon_31# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 387

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 391
if _libs["c-tinyusd"].has("c_tinyusd_string_new_empty", "cdecl"):
    c_tinyusd_string_new_empty = _libs["c-tinyusd"].get("c_tinyusd_string_new_empty", "cdecl")
    c_tinyusd_string_new_empty.argtypes = [POINTER(c_tinyusd_string)]
    c_tinyusd_string_new_empty.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 396
if _libs["c-tinyusd"].has("c_tinyusd_string_new", "cdecl"):
    c_tinyusd_string_new = _libs["c-tinyusd"].get("c_tinyusd_string_new", "cdecl")
    c_tinyusd_string_new.argtypes = [POINTER(c_tinyusd_string), String]
    c_tinyusd_string_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 399
if _libs["c-tinyusd"].has("c_tinyusd_string_size", "cdecl"):
    c_tinyusd_string_size = _libs["c-tinyusd"].get("c_tinyusd_string_size", "cdecl")
    c_tinyusd_string_size.argtypes = [POINTER(c_tinyusd_string)]
    c_tinyusd_string_size.restype = c_size_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 404
if _libs["c-tinyusd"].has("c_tinyusd_string_replace", "cdecl"):
    c_tinyusd_string_replace = _libs["c-tinyusd"].get("c_tinyusd_string_replace", "cdecl")
    c_tinyusd_string_replace.argtypes = [POINTER(c_tinyusd_string), String]
    c_tinyusd_string_replace.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 408
if _libs["c-tinyusd"].has("c_tinyusd_string_str", "cdecl"):
    c_tinyusd_string_str = _libs["c-tinyusd"].get("c_tinyusd_string_str", "cdecl")
    c_tinyusd_string_str.argtypes = [POINTER(c_tinyusd_string)]
    c_tinyusd_string_str.restype = c_char_p

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 410
if _libs["c-tinyusd"].has("c_tinyusd_string_free", "cdecl"):
    c_tinyusd_string_free = _libs["c-tinyusd"].get("c_tinyusd_string_free", "cdecl")
    c_tinyusd_string_free.argtypes = [POINTER(c_tinyusd_string)]
    c_tinyusd_string_free.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 414
class struct_anon_32(Structure):
    pass

struct_anon_32.__slots__ = [
    'data',
]
struct_anon_32._fields_ = [
    ('data', POINTER(None)),
]

c_tinyusd_string_vector = struct_anon_32# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 414

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 419
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_new_empty", "cdecl"):
    c_tinyusd_string_vector_new_empty = _libs["c-tinyusd"].get("c_tinyusd_string_vector_new_empty", "cdecl")
    c_tinyusd_string_vector_new_empty.argtypes = [POINTER(c_tinyusd_string_vector), c_size_t]
    c_tinyusd_string_vector_new_empty.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 421
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_new", "cdecl"):
    c_tinyusd_string_vector_new = _libs["c-tinyusd"].get("c_tinyusd_string_vector_new", "cdecl")
    c_tinyusd_string_vector_new.argtypes = [POINTER(c_tinyusd_string_vector), c_size_t, POINTER(POINTER(c_char))]
    c_tinyusd_string_vector_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 427
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_size", "cdecl"):
    c_tinyusd_string_vector_size = _libs["c-tinyusd"].get("c_tinyusd_string_vector_size", "cdecl")
    c_tinyusd_string_vector_size.argtypes = [POINTER(c_tinyusd_string_vector)]
    c_tinyusd_string_vector_size.restype = c_size_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 429
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_clear", "cdecl"):
    c_tinyusd_string_vector_clear = _libs["c-tinyusd"].get("c_tinyusd_string_vector_clear", "cdecl")
    c_tinyusd_string_vector_clear.argtypes = [POINTER(c_tinyusd_string_vector)]
    c_tinyusd_string_vector_clear.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 430
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_resize", "cdecl"):
    c_tinyusd_string_vector_resize = _libs["c-tinyusd"].get("c_tinyusd_string_vector_resize", "cdecl")
    c_tinyusd_string_vector_resize.argtypes = [POINTER(c_tinyusd_string_vector), c_size_t]
    c_tinyusd_string_vector_resize.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 436
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_str", "cdecl"):
    c_tinyusd_string_vector_str = _libs["c-tinyusd"].get("c_tinyusd_string_vector_str", "cdecl")
    c_tinyusd_string_vector_str.argtypes = [POINTER(c_tinyusd_string_vector), c_size_t]
    c_tinyusd_string_vector_str.restype = c_char_p

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 442
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_replace", "cdecl"):
    c_tinyusd_string_vector_replace = _libs["c-tinyusd"].get("c_tinyusd_string_vector_replace", "cdecl")
    c_tinyusd_string_vector_replace.argtypes = [POINTER(c_tinyusd_string_vector), c_size_t, String]
    c_tinyusd_string_vector_replace.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 444
if _libs["c-tinyusd"].has("c_tinyusd_string_vector_free", "cdecl"):
    c_tinyusd_string_vector_free = _libs["c-tinyusd"].get("c_tinyusd_string_vector_free", "cdecl")
    c_tinyusd_string_vector_free.argtypes = [POINTER(c_tinyusd_string_vector)]
    c_tinyusd_string_vector_free.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 450
if _libs["c-tinyusd"].has("c_tinyusd_prim_type_name", "cdecl"):
    c_tinyusd_prim_type_name = _libs["c-tinyusd"].get("c_tinyusd_prim_type_name", "cdecl")
    c_tinyusd_prim_type_name.argtypes = [CTinyUSDPrimType]
    c_tinyusd_prim_type_name.restype = c_char_p

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 454
if _libs["c-tinyusd"].has("c_tinyusd_prim_type_from_string", "cdecl"):
    c_tinyusd_prim_type_from_string = _libs["c-tinyusd"].get("c_tinyusd_prim_type_from_string", "cdecl")
    c_tinyusd_prim_type_from_string.argtypes = [String]
    c_tinyusd_prim_type_from_string.restype = CTinyUSDPrimType

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 464
class struct_anon_33(Structure):
    pass

struct_anon_33.__slots__ = [
    'value_type',
    'ndim',
    'shape',
    'data',
]
struct_anon_33._fields_ = [
    ('value_type', CTinyUSDValueType),
    ('ndim', uint32_t),
    ('shape', uint64_t * int(1)),
    ('data', POINTER(None)),
]

CTinyUSDBuffer = struct_anon_33# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 464

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 469
if _libs["c-tinyusd"].has("c_tinyusd_value_type_name", "cdecl"):
    c_tinyusd_value_type_name = _libs["c-tinyusd"].get("c_tinyusd_value_type_name", "cdecl")
    c_tinyusd_value_type_name.argtypes = [CTinyUSDValueType]
    c_tinyusd_value_type_name.restype = c_char_p

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 474
if _libs["c-tinyusd"].has("c_tinyusd_value_type_sizeof", "cdecl"):
    c_tinyusd_value_type_sizeof = _libs["c-tinyusd"].get("c_tinyusd_value_type_sizeof", "cdecl")
    c_tinyusd_value_type_sizeof.argtypes = [CTinyUSDValueType]
    c_tinyusd_value_type_sizeof.restype = uint32_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 480
if _libs["c-tinyusd"].has("c_tinyusd_value_type_components", "cdecl"):
    c_tinyusd_value_type_components = _libs["c-tinyusd"].get("c_tinyusd_value_type_components", "cdecl")
    c_tinyusd_value_type_components.argtypes = [CTinyUSDValueType]
    c_tinyusd_value_type_components.restype = uint32_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 490
if _libs["c-tinyusd"].has("c_tinyusd_buffer_new", "cdecl"):
    c_tinyusd_buffer_new = _libs["c-tinyusd"].get("c_tinyusd_buffer_new", "cdecl")
    c_tinyusd_buffer_new.argtypes = [POINTER(CTinyUSDBuffer), CTinyUSDValueType]
    c_tinyusd_buffer_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 495
if _libs["c-tinyusd"].has("c_tinyusd_buffer_new_and_copy_token", "cdecl"):
    c_tinyusd_buffer_new_and_copy_token = _libs["c-tinyusd"].get("c_tinyusd_buffer_new_and_copy_token", "cdecl")
    c_tinyusd_buffer_new_and_copy_token.argtypes = [POINTER(CTinyUSDBuffer), POINTER(c_tinyusd_token)]
    c_tinyusd_buffer_new_and_copy_token.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 500
if _libs["c-tinyusd"].has("c_tinyusd_buffer_new_and_copy_string", "cdecl"):
    c_tinyusd_buffer_new_and_copy_string = _libs["c-tinyusd"].get("c_tinyusd_buffer_new_and_copy_string", "cdecl")
    c_tinyusd_buffer_new_and_copy_string.argtypes = [POINTER(CTinyUSDBuffer), POINTER(c_tinyusd_string)]
    c_tinyusd_buffer_new_and_copy_string.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 504
if _libs["c-tinyusd"].has("c_tinyusd_buffer_new_array", "cdecl"):
    c_tinyusd_buffer_new_array = _libs["c-tinyusd"].get("c_tinyusd_buffer_new_array", "cdecl")
    c_tinyusd_buffer_new_array.argtypes = [POINTER(CTinyUSDBuffer), CTinyUSDValueType, uint64_t]
    c_tinyusd_buffer_new_array.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 507
if _libs["c-tinyusd"].has("c_tinyusd_buffer_free", "cdecl"):
    c_tinyusd_buffer_free = _libs["c-tinyusd"].get("c_tinyusd_buffer_free", "cdecl")
    c_tinyusd_buffer_free.argtypes = [POINTER(CTinyUSDBuffer)]
    c_tinyusd_buffer_free.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 511
class struct_anon_34(Structure):
    pass

struct_anon_34.__slots__ = [
    'buffer',
]
struct_anon_34._fields_ = [
    ('buffer', CTinyUSDBuffer),
]

CTinyUSDAttributeValue = struct_anon_34# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 511

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 526
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_to_string", "cdecl"):
    c_tinyusd_attribute_value_to_string = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_to_string", "cdecl")
    c_tinyusd_attribute_value_to_string.argtypes = [POINTER(CTinyUSDAttributeValue), POINTER(c_tinyusd_string)]
    c_tinyusd_attribute_value_to_string.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 530
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_free", "cdecl"):
        continue
    c_tinyusd_attribute_value_free = _lib.get("c_tinyusd_attribute_value_free", "cdecl")
    c_tinyusd_attribute_value_free.argtypes = [POINTER(CTinyUSDAttributeValue)]
    c_tinyusd_attribute_value_free.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 536
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_token", "cdecl"):
    c_tinyusd_attribute_value_new_token = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_token", "cdecl")
    c_tinyusd_attribute_value_new_token.argtypes = [POINTER(CTinyUSDAttributeValue), POINTER(c_tinyusd_token)]
    c_tinyusd_attribute_value_new_token.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 542
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_string", "cdecl"):
    c_tinyusd_attribute_value_new_string = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_string", "cdecl")
    c_tinyusd_attribute_value_new_string.argtypes = [POINTER(CTinyUSDAttributeValue), POINTER(c_tinyusd_string)]
    c_tinyusd_attribute_value_new_string.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 549
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_int", "cdecl"):
    c_tinyusd_attribute_value_new_int = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_int", "cdecl")
    c_tinyusd_attribute_value_new_int.argtypes = [POINTER(CTinyUSDAttributeValue), c_int]
    c_tinyusd_attribute_value_new_int.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 550
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_int2", "cdecl"):
    c_tinyusd_attribute_value_new_int2 = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_int2", "cdecl")
    c_tinyusd_attribute_value_new_int2.argtypes = [POINTER(CTinyUSDAttributeValue), c_tinyusd_int2]
    c_tinyusd_attribute_value_new_int2.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 551
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_int3", "cdecl"):
    c_tinyusd_attribute_value_new_int3 = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_int3", "cdecl")
    c_tinyusd_attribute_value_new_int3.argtypes = [POINTER(CTinyUSDAttributeValue), c_tinyusd_int3]
    c_tinyusd_attribute_value_new_int3.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 552
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_int4", "cdecl"):
    c_tinyusd_attribute_value_new_int4 = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_int4", "cdecl")
    c_tinyusd_attribute_value_new_int4.argtypes = [POINTER(CTinyUSDAttributeValue), c_tinyusd_int4]
    c_tinyusd_attribute_value_new_int4.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 553
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_float", "cdecl"):
    c_tinyusd_attribute_value_new_float = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_float", "cdecl")
    c_tinyusd_attribute_value_new_float.argtypes = [POINTER(CTinyUSDAttributeValue), c_float]
    c_tinyusd_attribute_value_new_float.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 554
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_float2", "cdecl"):
    c_tinyusd_attribute_value_new_float2 = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_float2", "cdecl")
    c_tinyusd_attribute_value_new_float2.argtypes = [POINTER(CTinyUSDAttributeValue), c_tinyusd_float2]
    c_tinyusd_attribute_value_new_float2.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 555
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_float3", "cdecl"):
    c_tinyusd_attribute_value_new_float3 = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_float3", "cdecl")
    c_tinyusd_attribute_value_new_float3.argtypes = [POINTER(CTinyUSDAttributeValue), c_tinyusd_float3]
    c_tinyusd_attribute_value_new_float3.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 556
if _libs["c-tinyusd"].has("c_tinyusd_attribute_value_new_float4", "cdecl"):
    c_tinyusd_attribute_value_new_float4 = _libs["c-tinyusd"].get("c_tinyusd_attribute_value_new_float4", "cdecl")
    c_tinyusd_attribute_value_new_float4.argtypes = [POINTER(CTinyUSDAttributeValue), c_tinyusd_float4]
    c_tinyusd_attribute_value_new_float4.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 563
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_int_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_int_array = _lib.get("c_tinyusd_attribute_value_new_int_array", "cdecl")
    c_tinyusd_attribute_value_new_int_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_int)]
    c_tinyusd_attribute_value_new_int_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 564
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_int2_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_int2_array = _lib.get("c_tinyusd_attribute_value_new_int2_array", "cdecl")
    c_tinyusd_attribute_value_new_int2_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_tinyusd_int2)]
    c_tinyusd_attribute_value_new_int2_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 565
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_int3_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_int3_array = _lib.get("c_tinyusd_attribute_value_new_int3_array", "cdecl")
    c_tinyusd_attribute_value_new_int3_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_tinyusd_int3)]
    c_tinyusd_attribute_value_new_int3_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 566
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_int4_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_int4_array = _lib.get("c_tinyusd_attribute_value_new_int4_array", "cdecl")
    c_tinyusd_attribute_value_new_int4_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_tinyusd_int4)]
    c_tinyusd_attribute_value_new_int4_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 567
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_float_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_float_array = _lib.get("c_tinyusd_attribute_value_new_float_array", "cdecl")
    c_tinyusd_attribute_value_new_float_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_float)]
    c_tinyusd_attribute_value_new_float_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 568
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_float2_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_float2_array = _lib.get("c_tinyusd_attribute_value_new_float2_array", "cdecl")
    c_tinyusd_attribute_value_new_float2_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_tinyusd_float2)]
    c_tinyusd_attribute_value_new_float2_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 569
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_float3_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_float3_array = _lib.get("c_tinyusd_attribute_value_new_float3_array", "cdecl")
    c_tinyusd_attribute_value_new_float3_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_tinyusd_float3)]
    c_tinyusd_attribute_value_new_float3_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 570
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_value_new_float4_array", "cdecl"):
        continue
    c_tinyusd_attribute_value_new_float4_array = _lib.get("c_tinyusd_attribute_value_new_float4_array", "cdecl")
    c_tinyusd_attribute_value_new_float4_array.argtypes = [POINTER(CTinyUSDAttributeValue), uint64_t, POINTER(c_tinyusd_float4)]
    c_tinyusd_attribute_value_new_float4_array.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 578
class struct_anon_35(Structure):
    pass

struct_anon_35.__slots__ = [
    'data',
]
struct_anon_35._fields_ = [
    ('data', POINTER(None)),
]

CTinyUSDPath = struct_anon_35# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 578

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 582
class struct_anon_36(Structure):
    pass

struct_anon_36.__slots__ = [
    'data',
]
struct_anon_36._fields_ = [
    ('data', POINTER(None)),
]

CTinyUSDProperty = struct_anon_36# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 582

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 590
class struct_anon_37(Structure):
    pass

struct_anon_37.__slots__ = [
    'data',
]
struct_anon_37._fields_ = [
    ('data', POINTER(None)),
]

CTinyUSDRelationship = struct_anon_37# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 590

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 592
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_relationsip_new", "cdecl"):
        continue
    c_tinyusd_relationsip_new = _lib.get("c_tinyusd_relationsip_new", "cdecl")
    c_tinyusd_relationsip_new.argtypes = [uint32_t, POINTER(POINTER(c_char))]
    c_tinyusd_relationsip_new.restype = POINTER(CTinyUSDRelationship)
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 596
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_relationsip_free", "cdecl"):
        continue
    c_tinyusd_relationsip_free = _lib.get("c_tinyusd_relationsip_free", "cdecl")
    c_tinyusd_relationsip_free.argtypes = [POINTER(CTinyUSDRelationship)]
    c_tinyusd_relationsip_free.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 598
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_relationsip_is_blocked", "cdecl"):
        continue
    c_tinyusd_relationsip_is_blocked = _lib.get("c_tinyusd_relationsip_is_blocked", "cdecl")
    c_tinyusd_relationsip_is_blocked.argtypes = [POINTER(CTinyUSDRelationship)]
    c_tinyusd_relationsip_is_blocked.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 601
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_relationsip_num_targetPaths", "cdecl"):
        continue
    c_tinyusd_relationsip_num_targetPaths = _lib.get("c_tinyusd_relationsip_num_targetPaths", "cdecl")
    c_tinyusd_relationsip_num_targetPaths.argtypes = [POINTER(CTinyUSDRelationship)]
    c_tinyusd_relationsip_num_targetPaths.restype = uint32_t
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 605
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_relationsip_get_targetPath", "cdecl"):
        continue
    c_tinyusd_relationsip_get_targetPath = _lib.get("c_tinyusd_relationsip_get_targetPath", "cdecl")
    c_tinyusd_relationsip_get_targetPath.argtypes = [POINTER(CTinyUSDRelationship), uint32_t, POINTER(CTinyUSDPath)]
    c_tinyusd_relationsip_get_targetPath.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 609
class struct_anon_38(Structure):
    pass

struct_anon_38.__slots__ = [
    'data',
]
struct_anon_38._fields_ = [
    ('data', POINTER(None)),
]

CTinyUSDAttribute = struct_anon_38# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 609

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 612
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_connection_set", "cdecl"):
        continue
    c_tinyusd_attribute_connection_set = _lib.get("c_tinyusd_attribute_connection_set", "cdecl")
    c_tinyusd_attribute_connection_set.argtypes = [POINTER(CTinyUSDAttribute), POINTER(CTinyUSDPath)]
    c_tinyusd_attribute_connection_set.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 615
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_attribute_connections_set", "cdecl"):
        continue
    c_tinyusd_attribute_connections_set = _lib.get("c_tinyusd_attribute_connections_set", "cdecl")
    c_tinyusd_attribute_connections_set.argtypes = [POINTER(CTinyUSDAttribute), uint32_t, POINTER(CTinyUSDPath)]
    c_tinyusd_attribute_connections_set.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 622
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_new", "cdecl"):
        continue
    c_tinyusd_property_new = _lib.get("c_tinyusd_property_new", "cdecl")
    c_tinyusd_property_new.argtypes = [POINTER(CTinyUSDProperty)]
    c_tinyusd_property_new.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 623
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_new_attribute", "cdecl"):
        continue
    c_tinyusd_property_new_attribute = _lib.get("c_tinyusd_property_new_attribute", "cdecl")
    c_tinyusd_property_new_attribute.argtypes = [POINTER(CTinyUSDProperty), POINTER(CTinyUSDAttribute)]
    c_tinyusd_property_new_attribute.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 624
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_new_relationship", "cdecl"):
        continue
    c_tinyusd_property_new_relationship = _lib.get("c_tinyusd_property_new_relationship", "cdecl")
    c_tinyusd_property_new_relationship.argtypes = [POINTER(CTinyUSDProperty), POINTER(CTinyUSDRelationship)]
    c_tinyusd_property_new_relationship.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 625
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_free", "cdecl"):
        continue
    c_tinyusd_property_free = _lib.get("c_tinyusd_property_free", "cdecl")
    c_tinyusd_property_free.argtypes = [POINTER(CTinyUSDProperty)]
    c_tinyusd_property_free.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 627
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_set_attribute", "cdecl"):
        continue
    c_tinyusd_property_set_attribute = _lib.get("c_tinyusd_property_set_attribute", "cdecl")
    c_tinyusd_property_set_attribute.argtypes = [POINTER(CTinyUSDProperty), POINTER(CTinyUSDAttribute)]
    c_tinyusd_property_set_attribute.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 628
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_set_relationship", "cdecl"):
        continue
    c_tinyusd_property_set_relationship = _lib.get("c_tinyusd_property_set_relationship", "cdecl")
    c_tinyusd_property_set_relationship.argtypes = [POINTER(CTinyUSDProperty), POINTER(CTinyUSDRelationship)]
    c_tinyusd_property_set_relationship.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 630
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_is_attribute", "cdecl"):
        continue
    c_tinyusd_property_is_attribute = _lib.get("c_tinyusd_property_is_attribute", "cdecl")
    c_tinyusd_property_is_attribute.argtypes = [POINTER(CTinyUSDProperty)]
    c_tinyusd_property_is_attribute.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 631
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_is_attribute_connection", "cdecl"):
        continue
    c_tinyusd_property_is_attribute_connection = _lib.get("c_tinyusd_property_is_attribute_connection", "cdecl")
    c_tinyusd_property_is_attribute_connection.argtypes = [POINTER(CTinyUSDProperty)]
    c_tinyusd_property_is_attribute_connection.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 632
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_property_is_relationship", "cdecl"):
        continue
    c_tinyusd_property_is_relationship = _lib.get("c_tinyusd_property_is_relationship", "cdecl")
    c_tinyusd_property_is_relationship.argtypes = [POINTER(CTinyUSDProperty)]
    c_tinyusd_property_is_relationship.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 640
class struct_anon_39(Structure):
    pass

struct_anon_39.__slots__ = [
    'data',
]
struct_anon_39._fields_ = [
    ('data', POINTER(None)),
]

CTinyUSDPrim = struct_anon_39# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 640

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 646
if _libs["c-tinyusd"].has("c_tinyusd_prim_new", "cdecl"):
    c_tinyusd_prim_new = _libs["c-tinyusd"].get("c_tinyusd_prim_new", "cdecl")
    c_tinyusd_prim_new.argtypes = [String, POINTER(CTinyUSDPrim)]
    c_tinyusd_prim_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 651
if _libs["c-tinyusd"].has("c_tinyusd_prim_new_builtin", "cdecl"):
    c_tinyusd_prim_new_builtin = _libs["c-tinyusd"].get("c_tinyusd_prim_new_builtin", "cdecl")
    c_tinyusd_prim_new_builtin.argtypes = [CTinyUSDPrimType, POINTER(CTinyUSDPrim)]
    c_tinyusd_prim_new_builtin.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 653
if _libs["c-tinyusd"].has("c_tinyusd_prim_to_string", "cdecl"):
    c_tinyusd_prim_to_string = _libs["c-tinyusd"].get("c_tinyusd_prim_to_string", "cdecl")
    c_tinyusd_prim_to_string.argtypes = [POINTER(CTinyUSDPrim), POINTER(c_tinyusd_string)]
    c_tinyusd_prim_to_string.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 655
if _libs["c-tinyusd"].has("c_tinyusd_prim_free", "cdecl"):
    c_tinyusd_prim_free = _libs["c-tinyusd"].get("c_tinyusd_prim_free", "cdecl")
    c_tinyusd_prim_free.argtypes = [POINTER(CTinyUSDPrim)]
    c_tinyusd_prim_free.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 664
if _libs["c-tinyusd"].has("c_tinyusd_prim_get_property_names", "cdecl"):
    c_tinyusd_prim_get_property_names = _libs["c-tinyusd"].get("c_tinyusd_prim_get_property_names", "cdecl")
    c_tinyusd_prim_get_property_names.argtypes = [POINTER(CTinyUSDPrim), POINTER(c_tinyusd_token_vector)]
    c_tinyusd_prim_get_property_names.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 668
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_prim_property_get", "cdecl"):
        continue
    c_tinyusd_prim_property_get = _lib.get("c_tinyusd_prim_property_get", "cdecl")
    c_tinyusd_prim_property_get.argtypes = [POINTER(CTinyUSDPrim), String, POINTER(CTinyUSDProperty)]
    c_tinyusd_prim_property_get.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 673
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_prim_property_add", "cdecl"):
        continue
    c_tinyusd_prim_property_add = _lib.get("c_tinyusd_prim_property_add", "cdecl")
    c_tinyusd_prim_property_add.argtypes = [POINTER(CTinyUSDPrim), String, POINTER(CTinyUSDProperty), POINTER(c_tinyusd_string)]
    c_tinyusd_prim_property_add.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 677
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_prim_property_del", "cdecl"):
        continue
    c_tinyusd_prim_property_del = _lib.get("c_tinyusd_prim_property_del", "cdecl")
    c_tinyusd_prim_property_del.argtypes = [POINTER(CTinyUSDPrim), String]
    c_tinyusd_prim_property_del.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 685
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_prim_append_child", "cdecl"):
        continue
    c_tinyusd_prim_append_child = _lib.get("c_tinyusd_prim_append_child", "cdecl")
    c_tinyusd_prim_append_child.argtypes = [POINTER(CTinyUSDPrim), POINTER(CTinyUSDPrim)]
    c_tinyusd_prim_append_child.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 689
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_prim_del_child", "cdecl"):
        continue
    c_tinyusd_prim_del_child = _lib.get("c_tinyusd_prim_del_child", "cdecl")
    c_tinyusd_prim_del_child.argtypes = [POINTER(CTinyUSDPrim), c_int]
    c_tinyusd_prim_del_child.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 696
if _libs["c-tinyusd"].has("c_tinyusd_prim_num_children", "cdecl"):
    c_tinyusd_prim_num_children = _libs["c-tinyusd"].get("c_tinyusd_prim_num_children", "cdecl")
    c_tinyusd_prim_num_children.argtypes = [POINTER(CTinyUSDPrim)]
    c_tinyusd_prim_num_children.restype = uint64_t

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 707
if _libs["c-tinyusd"].has("c_tinyusd_prim_get_child", "cdecl"):
    c_tinyusd_prim_get_child = _libs["c-tinyusd"].get("c_tinyusd_prim_get_child", "cdecl")
    c_tinyusd_prim_get_child.argtypes = [POINTER(CTinyUSDPrim), uint32_t, POINTER(POINTER(CTinyUSDPrim))]
    c_tinyusd_prim_get_child.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 712
class struct_anon_40(Structure):
    pass

struct_anon_40.__slots__ = [
    'data',
]
struct_anon_40._fields_ = [
    ('data', POINTER(None)),
]

CTinyUSDStage = struct_anon_40# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 712

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 714
if _libs["c-tinyusd"].has("c_tinyusd_stage_new", "cdecl"):
    c_tinyusd_stage_new = _libs["c-tinyusd"].get("c_tinyusd_stage_new", "cdecl")
    c_tinyusd_stage_new.argtypes = [POINTER(CTinyUSDStage)]
    c_tinyusd_stage_new.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 715
if _libs["c-tinyusd"].has("c_tinyusd_stage_to_string", "cdecl"):
    c_tinyusd_stage_to_string = _libs["c-tinyusd"].get("c_tinyusd_stage_to_string", "cdecl")
    c_tinyusd_stage_to_string.argtypes = [POINTER(CTinyUSDStage), POINTER(c_tinyusd_string)]
    c_tinyusd_stage_to_string.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 717
if _libs["c-tinyusd"].has("c_tinyusd_stage_free", "cdecl"):
    c_tinyusd_stage_free = _libs["c-tinyusd"].get("c_tinyusd_stage_free", "cdecl")
    c_tinyusd_stage_free.argtypes = [POINTER(CTinyUSDStage)]
    c_tinyusd_stage_free.restype = c_int

CTinyUSDTraversalFunction = CFUNCTYPE(UNCHECKED(c_int), POINTER(CTinyUSDPrim), POINTER(CTinyUSDPath))# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 721

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 737
if _libs["c-tinyusd"].has("c_tinyusd_stage_traverse", "cdecl"):
    c_tinyusd_stage_traverse = _libs["c-tinyusd"].get("c_tinyusd_stage_traverse", "cdecl")
    c_tinyusd_stage_traverse.argtypes = [POINTER(CTinyUSDStage), CTinyUSDTraversalFunction, POINTER(c_tinyusd_string)]
    c_tinyusd_stage_traverse.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 745
if _libs["c-tinyusd"].has("c_tinyusd_detect_format", "cdecl"):
    c_tinyusd_detect_format = _libs["c-tinyusd"].get("c_tinyusd_detect_format", "cdecl")
    c_tinyusd_detect_format.argtypes = [String]
    c_tinyusd_detect_format.restype = CTinyUSDFormat

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 747
if _libs["c-tinyusd"].has("c_tinyusd_is_usd_file", "cdecl"):
    c_tinyusd_is_usd_file = _libs["c-tinyusd"].get("c_tinyusd_is_usd_file", "cdecl")
    c_tinyusd_is_usd_file.argtypes = [String]
    c_tinyusd_is_usd_file.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 748
if _libs["c-tinyusd"].has("c_tinyusd_is_usda_file", "cdecl"):
    c_tinyusd_is_usda_file = _libs["c-tinyusd"].get("c_tinyusd_is_usda_file", "cdecl")
    c_tinyusd_is_usda_file.argtypes = [String]
    c_tinyusd_is_usda_file.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 749
if _libs["c-tinyusd"].has("c_tinyusd_is_usdc_file", "cdecl"):
    c_tinyusd_is_usdc_file = _libs["c-tinyusd"].get("c_tinyusd_is_usdc_file", "cdecl")
    c_tinyusd_is_usdc_file.argtypes = [String]
    c_tinyusd_is_usdc_file.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 750
if _libs["c-tinyusd"].has("c_tinyusd_is_usdz_file", "cdecl"):
    c_tinyusd_is_usdz_file = _libs["c-tinyusd"].get("c_tinyusd_is_usdz_file", "cdecl")
    c_tinyusd_is_usdz_file.argtypes = [String]
    c_tinyusd_is_usdz_file.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 752
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_is_usd_memory", "cdecl"):
        continue
    c_tinyusd_is_usd_memory = _lib.get("c_tinyusd_is_usd_memory", "cdecl")
    c_tinyusd_is_usd_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_tinyusd_is_usd_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 753
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_is_usda_memory", "cdecl"):
        continue
    c_tinyusd_is_usda_memory = _lib.get("c_tinyusd_is_usda_memory", "cdecl")
    c_tinyusd_is_usda_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_tinyusd_is_usda_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 754
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_is_usdc_memory", "cdecl"):
        continue
    c_tinyusd_is_usdc_memory = _lib.get("c_tinyusd_is_usdc_memory", "cdecl")
    c_tinyusd_is_usdc_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_tinyusd_is_usdc_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 755
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_is_usdz_memory", "cdecl"):
        continue
    c_tinyusd_is_usdz_memory = _lib.get("c_tinyusd_is_usdz_memory", "cdecl")
    c_tinyusd_is_usdz_memory.argtypes = [POINTER(uint8_t), c_size_t]
    c_tinyusd_is_usdz_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 757
if _libs["c-tinyusd"].has("c_tinyusd_load_usd_from_file", "cdecl"):
    c_tinyusd_load_usd_from_file = _libs["c-tinyusd"].get("c_tinyusd_load_usd_from_file", "cdecl")
    c_tinyusd_load_usd_from_file.argtypes = [String, POINTER(CTinyUSDStage), POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usd_from_file.restype = c_int

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 759
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usda_from_file", "cdecl"):
        continue
    c_tinyusd_load_usda_from_file = _lib.get("c_tinyusd_load_usda_from_file", "cdecl")
    c_tinyusd_load_usda_from_file.argtypes = [String, POINTER(CTinyUSDStage), POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usda_from_file.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 762
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usdc_from_file", "cdecl"):
        continue
    c_tinyusd_load_usdc_from_file = _lib.get("c_tinyusd_load_usdc_from_file", "cdecl")
    c_tinyusd_load_usdc_from_file.argtypes = [String, POINTER(CTinyUSDStage), POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usdc_from_file.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 765
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usdz_from_file", "cdecl"):
        continue
    c_tinyusd_load_usdz_from_file = _lib.get("c_tinyusd_load_usdz_from_file", "cdecl")
    c_tinyusd_load_usdz_from_file.argtypes = [String, POINTER(CTinyUSDStage), POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usdz_from_file.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 769
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usd_from_memory", "cdecl"):
        continue
    c_tinyusd_load_usd_from_memory = _lib.get("c_tinyusd_load_usd_from_memory", "cdecl")
    c_tinyusd_load_usd_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usd_from_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 772
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usda_from_memory", "cdecl"):
        continue
    c_tinyusd_load_usda_from_memory = _lib.get("c_tinyusd_load_usda_from_memory", "cdecl")
    c_tinyusd_load_usda_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usda_from_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 775
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usdc_from_memory", "cdecl"):
        continue
    c_tinyusd_load_usdc_from_memory = _lib.get("c_tinyusd_load_usdc_from_memory", "cdecl")
    c_tinyusd_load_usdc_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usdc_from_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 778
for _lib in _libs.values():
    if not _lib.has("c_tinyusd_load_usdz_from_memory", "cdecl"):
        continue
    c_tinyusd_load_usdz_from_memory = _lib.get("c_tinyusd_load_usdz_from_memory", "cdecl")
    c_tinyusd_load_usdz_from_memory.argtypes = [POINTER(uint8_t), c_size_t, POINTER(c_tinyusd_string), POINTER(c_tinyusd_string)]
    c_tinyusd_load_usdz_from_memory.restype = c_int
    break

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 51
try:
    C_TINYUSD_MAX_DIM = 1
except:
    pass

# /home/syoyo/work/tinyusdz/src/c-tinyusd.h: 137
try:
    C_TINYUSD_VALUE_1D_BIT = (1 << 10)
except:
    pass

# No inserted files

# No prefix-stripping

