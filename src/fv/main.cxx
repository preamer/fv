#include "vtkFLUENTCFFReader.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

py::dict read_mesh_data(const std::string& filename)
{
    vtkFLUENTCFFReader reader;
    return reader.ReadMeshData(filename);
}

py::object read_pyvista_mesh(const std::string& filename)
{
    vtkFLUENTCFFReader reader;
    return reader.ReadPyVistaMesh(filename);
}

PYBIND11_MODULE(mesh_reader, m)
{
    m.doc() = "Fluent .cas.h5 reader for PyVista";
    m.def("read_mesh_data", &read_mesh_data);
    m.def("read_pyvista_mesh", &read_pyvista_mesh);
}
