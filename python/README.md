# Python binding of TinyUSDZ

Currently in testing stage. Does not work well.

Core part is deletegated to native module(ctinyusd.so)

W.I.P.

## Requirements

* Python 3.8 or later
  * tinyusdz python binding uses Python 3.8 features(e.g. Literal type)
  * Python 3.12+ recommended

### Recommended

* numpy
  * For efficient Attribute data handling.
* pandas
  * To support TimeSamples data efficiently(e.g. read/write to CSV, Excel)

## Structure

* `ctinyusdz`: Native C++ module of tinyusdz
  * Python binding using pybind11. Binding source code is located at `<tinyusdz>/src/python-binding.cc`
* `tinyusdz`: Python module. Wraps some functions of `ctinyusdz`

TinyUSDZ's Python binding approach is like numpy: entry point is written in Python for better Python integration(type hints for lsp(Intellisense), debuggers, exceptions, ...), and calls native modules as necessary.

## Supported platform

* [ ] Linux
  * [x] x86-64
  * [ ] aarch64
* [ ] Windows
* [ ] macOS

## Features


### Optional

* [ ] pxrUSD compatible Python API?(`pxr_compat` folder)

## Install through PyPI

```
$ python -m pip install tinyusdz
```

## For developers. Build from source

Back to tinyusdz's root directory, then

```
# Use `build` module(install it with `python -m pip install build`) 
$ python -m build .
```

If you are working on TinyUSDZ Python module, Using `setup.py` recommended. 

```
$ python setup.py build
# Then copy `./_skbuild/<arch>-<version>/cmake-install/tinyusdz/ctinyusdz.*.so/dll to `<tinyusdz>/python` folder.
```


## License

Currently MIT license, but soon move to Apache 2.0 license.

EoL.
