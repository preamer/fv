import os
import sys
import warnings
from pathlib import Path

import pybind11
import numpy as np
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext


def _has_hdf5_header(d: Path) -> bool:
    return (d / "hdf5.h").exists()


def find_hdf5() -> tuple[list, list, list, list, list]:
    """Returns (include_dirs, library_dirs, libraries, define_macros, extra_link_args)."""
    include_dirs: list[str] = []
    library_dirs: list[str] = []
    libraries:    list[str] = ["hdf5"]
    define_macros:   list = []
    extra_link_args: list = []

    hdf5_dir = os.environ.get("HDF5_DIR", "").strip()
    if hdf5_dir:
        p = Path(hdf5_dir)
        if (p / "include").is_dir():
            include_dirs.append(str(p / "include"))
        if (p / "lib").is_dir():
            library_dirs.append(str(p / "lib"))

    if not include_dirs:
        local_inc, local_lib = Path("include"), Path("lib")
        if _has_hdf5_header(local_inc):
            include_dirs.append(str(local_inc))
        if local_lib.is_dir() and any(local_lib.iterdir()):
            library_dirs.append(str(local_lib))

        if sys.platform == "win32":
            define_macros.append(("H5_BUILT_AS_DYNAMIC_LIB", None))
            conda_base = Path(sys.prefix) / "Library"
            if _has_hdf5_header(conda_base / "include"):
                include_dirs.append(str(conda_base / "include"))
                library_dirs.append(str(conda_base / "lib"))
        elif sys.platform.startswith("linux"):
            for candidate in [Path("/usr/include/hdf5/serial"), Path("/usr/include")]:
                if _has_hdf5_header(candidate):
                    include_dirs.append(str(candidate))
                    break
            for candidate in [Path("/usr/lib/x86_64-linux-gnu/hdf5/serial"), Path("/usr/lib64")]:
                if candidate.is_dir():
                    library_dirs.append(str(candidate))
                    break

    if sys.platform == "win32":
        define_macros.append(("H5_BUILT_AS_DYNAMIC_LIB", None))
        libraries = ["libhdf5"]
    else:
        for ldir in library_dirs:
            extra_link_args.append(f"-Wl,-rpath,{ldir}")

    if not any(_has_hdf5_header(Path(d)) for d in include_dirs):
        warnings.warn(
            f"\n\n*** Could not locate hdf5.h inside {include_dirs} — build will likely fail. ***\n"
            "Please check if HDF5_DIR is set correctly.\n",
            stacklevel=2
        )

    return include_dirs, library_dirs, libraries, define_macros, extra_link_args


def get_compile_args() -> list[str]:
    if sys.platform == "win32":
        return ["/utf-8", "/EHsc", "/bigobj", "/O2"]
    return ["-O2", "-fvisibility=hidden"]


# ---------------------------------------------------------------------------
# Extension module
# ---------------------------------------------------------------------------
hdf5_inc, hdf5_lib, hdf5_libs, macros, link_args = find_hdf5()

ext_modules = [
    Pybind11Extension(
        "cffview.mesh_reader",
        sources=[
            "src/cffview/main.cxx",
            "src/cffview/vtkFLUENTCFFReader.cxx",
        ],
        include_dirs=[
            *hdf5_inc,
            str(Path("include")),
            np.get_include(),
            pybind11.get_include(),
        ],
        library_dirs=[*hdf5_lib, str(Path("lib"))],
        libraries=hdf5_libs,
        define_macros=macros,
        extra_compile_args=get_compile_args(),
        extra_link_args=link_args,
        cxx_std=17,
    ),
]

setup(
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
