# based on https://stackoverflow.com/questions/42585210/extending-setuptools-extension-to-use-cmake-in-setup-py
import os
import pathlib

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext as build_ext_orig


class CMakeExtension(Extension):

    def __init__(self, name):
        # don't invoke the original build_ext for this special extension
        super().__init__(name, sources=[])


class build_ext(build_ext_orig):

    def run(self):
        for ext in self.extensions:
            self.build_cmake(ext)
        super().run()

    def build_cmake(self, ext):
        cwd = pathlib.Path().absolute()
        print("cwd = ", cwd)

        # these dirs will be created in build_py, so if you don't have
        # any python sources to bundle, the dirs will be missing
        build_temp = pathlib.Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)
        extdir = pathlib.Path(self.get_ext_fullpath(ext.name))
        extdir.mkdir(parents=True, exist_ok=True)

        config = 'RelWithDebInfo'
        cmake_args = [
            '-S{}'.format(os.path.join(cwd, "..")),
            '-B{}'.format(build_temp),
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + str(extdir.parent.absolute()),
            '-DCMAKE_BUILD_TYPE=' + config
        ]

        # example of build args
        build_args = [
            '--config', config,
            '--', '-j4'
        ]

        print("cmake_args = ", cmake_args)

        #os.chdir(str(build_temp))
        self.spawn(['cmake'] + cmake_args)
        if not self.dry_run:
            self.spawn(['cmake', '--build', str(build_temp)] + build_args)
        # Troubleshooting: if fail on line above then delete all possible 
        # temporary CMake files including "CMakeCache.txt" in top level dir.
        os.chdir(str(cwd))


setup(
    name='pytinyusdz',
    version='0.8.0rc0',
    packages=['pytinyusdz'],
    long_description=open("../README.md", 'r').read(),
    ext_modules=[CMakeExtension('pytinyusdz')],
    license='MIT',
    cmdclass={
        'build_ext': build_ext,
    }
)
