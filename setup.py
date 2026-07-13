import pybind11
import numpy as np
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "fv.mesh_reader",
        ["src/fv/main.cxx", "src/fv/vtkFLUENTCFFReader.cxx"],
        include_dirs=["include", np.get_include(), pybind11.get_include()],
        library_dirs=["lib"],
        libraries=["hdf5"],
    ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
