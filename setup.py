import sys

import pybind11
import numpy as np
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

define_macros = []
extra_compile_args = []
extra_link_args = []

if sys.platform == "win32":
    define_macros.append(("H5_BUILT_AS_DYNAMIC_LIB", None))
    extra_compile_args.extend(["/utf-8", "/EHsc", "/bigobj"])
elif sys.platform == "linux":
    extra_link_args = ["-Wl,-rpath,$ORIGIN/../../lib"]

ext_modules = [
    Pybind11Extension(
        "fv.mesh_reader",
        ["src/fv/main.cxx", "src/fv/vtkFLUENTCFFReader.cxx"],
        include_dirs=["include", np.get_include(), pybind11.get_include()],
        library_dirs=["lib"],
        libraries=["hdf5"],
        define_macros=define_macros,
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
    ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
