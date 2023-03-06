# Python binding of TinyUSDZ

Currently in testing stage. Does not work well.

Core part is deletegated to native module(ctinyusd.so)

W.I.P.

## Requirements

* Python 3.8 or later
  * tinyusdz python binding uses Python 3.8 features(e.g. Literal type)
  * Python 3.12+ recommended

### Recommended

* numpy to use `from_numpy` and `to_numpy` method.

## Structure

* `ctinyusdz`: Native C++ module of tinyusdz
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

## Build from source

Back to tinyusdz's root directory, then

```
# Use `build` module(install it with `python -m pip install build`) 
$ python -m build .

# Or use setuptools approach.
$ python setup.py build
```


## License

Currently MIT license, but soon move to Apache 2.0 license.

EoL.
