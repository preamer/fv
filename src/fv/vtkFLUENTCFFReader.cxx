// A modified version of the original vtkFLUENTCFFReader.cxx

#include "vtkFLUENTCFFReader.h"
#include "pybind11/pybind11.h"
#include "pybind11/numpy.h"
#include <hdf5.h>

#include <iostream>

#define CHECK_HDF(fct)                                                                             \
  if (fct < 0)                                                                                     \
  throw std::runtime_error("HDF5 error in vtkFLUENTCFFReader: " + std::string(__func__) + " at " + \
    std::to_string(__LINE__))


namespace py = pybind11;

namespace {
    int FluentCellTypeToVtk(int type)
    {
        switch (type)
        {
        case 1: return 5;   // triangle
        case 2: return 10;  // tetra
        case 3: return 9;   // quad
        case 4: return 12;  // hexahedron
        case 5: return 14;  // pyramid
        case 6: return 13;  // wedge
        default: return 42; // polyhedron
        }
    }
}

//------------------------------------------------------------------------------
struct vtkFLUENTCFFReader::vtkInternals
{
    hid_t FluentCaseFile;
    hid_t FluentDataFile;
};

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::vtkFLUENTCFFReader() : HDFImpl(new vtkFLUENTCFFReader::vtkInternals)
{
    this->HDFImpl->FluentCaseFile = -1;
    this->HDFImpl->FluentDataFile = -1;
    H5Eset_auto(H5E_DEFAULT, nullptr, nullptr);
}

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::~vtkFLUENTCFFReader() = default;

py::dict vtkFLUENTCFFReader::ReadMeshData(const std::string& filename)
{
    if (!this->OpenCaseFile(filename))
        throw std::runtime_error("failed to open case file");

    this->FileName = filename;
    if (this->ParseCaseFile() == 0)
        throw std::runtime_error("failed to parse case file");

    this->CleanCells();
    this->PopulateCellNodes();
    this->GetNumberOfCellZones();

    std::vector<int> connectivity;
    std::vector<int> cell_types;
    std::vector<int> cell_zones;

    for (const auto& cell : this->Cells)
    {
        connectivity.push_back(static_cast<int>(cell.nodes.size()));
        connectivity.insert(connectivity.end(), cell.nodes.begin(), cell.nodes.end());
        cell_types.push_back(FluentCellTypeToVtk(cell.type));
        cell_zones.push_back(cell.zone);
    }

    auto points_shape = py::ssize_t(this->Points.size() / 3);
    py::array_t<double> points(std::vector<ssize_t>{ static_cast<ssize_t>(points_shape), 3 });
    auto points_mut = points.mutable_unchecked<2>();
    for (std::size_t i = 0; i < this->Points.size() / 3; ++i)
    {
        points_mut(i, 0) = this->Points[3 * i + 0];
        points_mut(i, 1) = this->Points[3 * i + 1];
        points_mut(i, 2) = this->Points[3 * i + 2];
    }

    py::array_t<int> cells(connectivity.size());
    auto cells_mut = cells.mutable_unchecked<1>();
    for (std::size_t i = 0; i < connectivity.size(); ++i)
        cells_mut(i) = connectivity[i];

    py::array_t<int> types(cell_types.size());
    auto types_mut = types.mutable_unchecked<1>();
    for (std::size_t i = 0; i < cell_types.size(); ++i)
        types_mut(i) = cell_types[i];

    py::array_t<int> zones(cell_zones.size());
    auto zones_mut = zones.mutable_unchecked<1>();
    for (std::size_t i = 0; i < cell_zones.size(); ++i)
        zones_mut(i) = cell_zones[i];

    py::dict result;
    result["points"] = points;
    result["cells"] = cells;
    result["cell_types"] = types;
    result["cell_zones"] = zones;
    return result;
}

py::object vtkFLUENTCFFReader::ReadPyVistaMesh(const std::string& filename)
{
    auto data = this->ReadMeshData(filename);
    py::module_ pyvista = py::module_::import("pyvista");
    return pyvista.attr("UnstructuredGrid")(data["cells"], data["cell_types"], data["points"]);
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::OpenCaseFile(const std::string& filename)
{
    // Check if hdf5 lib contains zlib (DEFLATE)
    htri_t avail = H5Zfilter_avail(H5Z_FILTER_DEFLATE);
    if (!avail)
    {
        std::cerr << "The current build is not compatible with this reader, HDF5 library misses ZLIB compatibility.";
        return false;
    }
    // Check if the file is HDF5 or exist
    htri_t file_type = H5Fis_hdf5(filename.c_str());
    if (file_type != 1)
    {
        std::cerr << "The file " << filename << " does not exist or is not a HDF5 file.";
        return false;
    }
    // Open file with default properties access
    this->HDFImpl->FluentCaseFile = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    // Check if file is CFF Format like
    herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes", false, nullptr);
    herr_t s2 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/settings", false, nullptr);
    if (s1 == 0 && s2 == 0)
    {
        return true;
    }
    else
    {
        std::cerr << "The file " << filename << " is not a CFF Fluent file.";
        return false;
    }
}

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::DataState vtkFLUENTCFFReader::OpenDataFile(const std::string& filename)
{
    // dfilename represent the dat file name (extension .dat.h5)
    // when opening a .cas.h5, it will automatically open the associated .dat.h5 (if exist)
    // filename.cas.h5 -> filename.dat.h5
    std::string dfilename = filename;
    dfilename.erase(dfilename.length() - 6, 6);
    dfilename.append("dat.h5");

    // Check if the file is HDF5 or exist
    htri_t file_type = H5Fis_hdf5(dfilename.c_str());
    // If there is a file but is not HDF5
    if (file_type == 0)
    {
        return DataState::ERROR;
    }
    // If there is no file, read only the case file
    if (file_type < 0)
    {
        return DataState::NOT_LOADED;
    }

    // Open file with default properties access
    this->HDFImpl->FluentDataFile = H5Fopen(dfilename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    return DataState::AVAILABLE;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNumberOfCellZones()
{
    for (const auto& cell : this->Cells)
    {
        if (this->CellZones.empty())
        {
            this->CellZones.push_back(cell.zone);
        }
        else
        {
            int match = 0;
            for (const auto& CellZone : CellZones)
            {
                if (CellZone == cell.zone)
                {
                    match = 1;
                }
            }
            if (match == 0)
            {
                this->CellZones.push_back(cell.zone);
            }
        }
    }
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::ParseCaseFile()
{
    try
    {
        this->GetNodesGlobal();
        this->GetCellsGlobal();
        this->GetFacesGlobal();
        // .cas is always DP
        // .dat is DP or SP
        this->GetNodes();
        this->GetCells();
        this->GetFaces();

        this->GetCellTree();
        this->GetCellOverset();
        this->GetFaceTree();
        this->GetInterfaceFaceParents();
        this->GetNonconformalGridInterfaceFaceInformation();
    }
    catch (std::runtime_error const& e)
    {
        std::cerr << e.what();
        return 0;
    }
    return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetDimension()
{
    hid_t group, attr;
    int32_t dimension;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
    if (group < 0)
    {
        std::cerr << "Unable to open HDF group (GetDimension).";
        return 0;
    }
    attr = H5Aopen(group, "dimension", H5P_DEFAULT);
    if (attr < 0)
    {
        std::cerr << "Unable to open HDF attribute (GetDimension).";
        return 0;
    }
    if (H5Aread(attr, H5T_NATIVE_INT32, &dimension) < 0)
    {
        std::cerr << "Unable to read HDF attribute (GetDimension).";
        return 0;
    }
    if (H5Aclose(attr))
    {
        std::cerr << "Unable to close HDF attribute (GetDimension).";
        return 0;
    }
    if (H5Gclose(group))
    {
        std::cerr << "Unable to close HDF group (GetDimension).";
        return 0;
    }
    return static_cast<int>(dimension);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNodesGlobal()
{
    hid_t group, attr;
    uint64_t firstIndex, lastIndex;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetNodesGlobal).");
    }
    attr = H5Aopen(group, "nodeOffset", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetNodesGlobal).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &firstIndex));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "nodeCount", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetNodesGlobal).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &lastIndex));
    CHECK_HDF(H5Aclose(attr));
    CHECK_HDF(H5Gclose(group));
    this->Points.reserve(lastIndex);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNodes()
{
    hid_t group, attr, dset;
    uint64_t nZones;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/nodes/zoneTopology", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetNodes).");
    }
    attr = H5Aopen(group, "nZones", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetNodes).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
    CHECK_HDF(H5Aclose(attr));

    std::vector<uint64_t> minId(nZones);
    dset = H5Dopen(group, "minId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, minId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<uint64_t> maxId(nZones);
    dset = H5Dopen(group, "maxId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, maxId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> Id(nZones);
    dset = H5Dopen(group, "id", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, Id.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<uint64_t> dimension(nZones);
    dset = H5Dopen(group, "dimension", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, dimension.data()));
    CHECK_HDF(H5Dclose(dset));

    for (uint64_t iZone = 0; iZone < nZones; iZone++)
    {
        uint64_t coords_minId, coords_maxId;
        hid_t group_coords, dset_coords;
        group_coords = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/nodes/coords", H5P_DEFAULT);
        if (group_coords < 0)
        {
            throw std::runtime_error("Unable to open HDF group (GetNodes coords).");
        }
        dset_coords = H5Dopen(group_coords, std::to_string(Id[iZone]).c_str(), H5P_DEFAULT);
        if (dset_coords < 0)
        {
            throw std::runtime_error("Unable to open HDF group (GetNodes coords).");
        }

        attr = H5Aopen(dset_coords, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetNodes coords).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &coords_minId));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(dset_coords, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetNodes coords).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &coords_maxId));
        CHECK_HDF(H5Aclose(attr));
        unsigned int firstIndex = static_cast<unsigned int>(coords_minId);
        unsigned int lastIndex = static_cast<unsigned int>(coords_maxId);

        uint64_t size = lastIndex - firstIndex + 1;
        uint64_t gSize;
        if (this->GridDimension == 3)
        {
            gSize = size * 3;
        }
        else
        {
            gSize = size * 2;
        }

        std::vector<double> nodeData(gSize);
        CHECK_HDF(
            H5Dread(dset_coords, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, nodeData.data()));
        CHECK_HDF(H5Dclose(dset_coords));
        CHECK_HDF(H5Gclose(group_coords));

        unsigned int requiredSize = lastIndex * 3;
        if (this->Points.size() < requiredSize) {
            this->Points.resize(requiredSize, 0.0); // 缺失的坐标默认用 0.0 填补
        }

        if (this->GridDimension == 3)
        {
            for (unsigned int i = firstIndex; i <= lastIndex; i++)
            {
                unsigned int pointId = i - 1;
                unsigned int nodeOffset = (i - firstIndex) * 3;

                this->Points[pointId * 3 + 0] = nodeData[nodeOffset + 0]; // X
                this->Points[pointId * 3 + 1] = nodeData[nodeOffset + 1]; // Y
                this->Points[pointId * 3 + 2] = nodeData[nodeOffset + 2]; // Z
            }
        }
        else
        {
            for (unsigned int i = firstIndex; i <= lastIndex; i++)
            {
                unsigned int pointId = i - 1;
                unsigned int nodeOffset = (i - firstIndex) * 2;

                this->Points[pointId * 3 + 0] = nodeData[nodeOffset + 0]; // X
                this->Points[pointId * 3 + 1] = nodeData[nodeOffset + 1]; // Y
                this->Points[pointId * 3 + 2] = 0.0;                      // Z
            }
        }
    }

    CHECK_HDF(H5Gclose(group));
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCellsGlobal()
{
    hid_t group, attr;
    uint64_t firstIndex, lastIndex;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetCellsGlobal).");
    }
    attr = H5Aopen(group, "cellOffset", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetCellsGlobal).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &firstIndex));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "cellCount", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetCellsGlobal).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &lastIndex));
    CHECK_HDF(H5Aclose(attr));
    CHECK_HDF(H5Gclose(group));
    this->Cells.resize(lastIndex);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCells()
{
    hid_t group, attr, dset;
    uint64_t nZones;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/zoneTopology", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetCells).");
    }
    attr = H5Aopen(group, "nZones", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetCells).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
    CHECK_HDF(H5Aclose(attr));

    std::vector<uint64_t> minId(nZones);
    dset = H5Dopen(group, "minId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetCells).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, minId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<uint64_t> maxId(nZones);
    dset = H5Dopen(group, "maxId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetCells).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, maxId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> Id(nZones);
    dset = H5Dopen(group, "id", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetCells).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, Id.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<uint64_t> dimension(nZones);
    dset = H5Dopen(group, "dimension", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetCells).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, dimension.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> cellType(nZones);
    dset = H5Dopen(group, "cellType", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetCells).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, cellType.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> childZoneId(nZones);
    dset = H5Dopen(group, "childZoneId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetCells).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, childZoneId.data()));
    CHECK_HDF(H5Dclose(dset));

    for (uint64_t iZone = 0; iZone < nZones; iZone++)
    {
        unsigned int elementType = static_cast<unsigned int>(cellType[iZone]);
        unsigned int zoneId = static_cast<unsigned int>(Id[iZone]);
        unsigned int firstIndex = static_cast<unsigned int>(minId[iZone]);
        unsigned int lastIndex = static_cast<unsigned int>(maxId[iZone]);
        // This next line should be uncommented following test with Fluent file
        // containing tree format (AMR)
        //// unsigned int child = static_cast<unsigned int>(childZoneId[iZone]);
        // next child and parent variable should be initialized correctly

        if (elementType == 0)
        {
            std::vector<int16_t> cellTypeData;
            hid_t group_ctype;
            uint64_t nSections;
            group_ctype = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/ctype", H5P_DEFAULT);
            if (group_ctype < 0)
            {
                throw std::runtime_error("Unable to open HDF group (GetCells ctype).");
            }
            attr = H5Aopen(group_ctype, "nSections", H5P_DEFAULT);
            if (attr < 0)
            {
                throw std::runtime_error("Unable to open HDF attribute (GetCells ctype).");
            }
            CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
            CHECK_HDF(H5Aclose(attr));
            CHECK_HDF(H5Gclose(group_ctype));

            // Search for ctype section linked to the mixed zone
            uint64_t ctype_minId = 0, ctype_maxId = 0;
            for (uint64_t iSection = 0; iSection < nSections; iSection++)
            {
                int16_t ctype_elementType;
                std::string groupname =
                    std::string("/meshes/1/cells/ctype/" + std::to_string(iSection + 1));
                group_ctype = H5Gopen(this->HDFImpl->FluentCaseFile, groupname.c_str(), H5P_DEFAULT);
                if (group_ctype < 0)
                {
                    throw std::runtime_error("Unable to open HDF group (GetCells ctype section).");
                }

                attr = H5Aopen(group_ctype, "elementType", H5P_DEFAULT);
                if (attr < 0)
                {
                    throw std::runtime_error("Unable to open HDF attribute (GetCells ctype section).");
                }
                CHECK_HDF(H5Aread(attr, H5T_NATIVE_INT16, &ctype_elementType));
                CHECK_HDF(H5Aclose(attr));
                attr = H5Aopen(group_ctype, "minId", H5P_DEFAULT);
                if (attr < 0)
                {
                    throw std::runtime_error("Unable to open HDF attribute (GetCells ctype section).");
                }
                CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &ctype_minId));
                CHECK_HDF(H5Aclose(attr));
                attr = H5Aopen(group_ctype, "maxId", H5P_DEFAULT);
                if (attr < 0)
                {
                    throw std::runtime_error("Unable to open HDF attribute (GetCells ctype section).");
                }
                CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &ctype_maxId));
                CHECK_HDF(H5Aclose(attr));

                if (static_cast<unsigned int>(ctype_elementType) == elementType &&
                    static_cast<unsigned int>(ctype_minId) <= firstIndex &&
                    static_cast<unsigned int>(ctype_maxId) >= lastIndex)
                {
                    cellTypeData.resize(ctype_maxId - ctype_minId + 1);
                    dset = H5Dopen(group_ctype, "cell-types", H5P_DEFAULT);
                    if (dset < 0)
                    {
                        throw std::runtime_error("Unable to open HDF dataset (GetCells ctype section).");
                    }
                    CHECK_HDF(
                        H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, cellTypeData.data()));
                    CHECK_HDF(H5Dclose(dset));
                    CHECK_HDF(H5Gclose(group_ctype));
                    break;
                }
                CHECK_HDF(H5Gclose(group_ctype));
            }

            if (!cellTypeData.empty())
            {
                for (unsigned int i = firstIndex; i <= lastIndex; i++)
                {
                    this->Cells[i - 1].type = static_cast<unsigned int>(cellTypeData[i - ctype_minId]);
                    this->Cells[i - 1].zone = zoneId;
                    this->Cells[i - 1].parent = 0;
                    this->Cells[i - 1].child = 0;
                }
            }
        }
        else
        {
            for (unsigned int i = firstIndex; i <= lastIndex; i++)
            {
                this->Cells[i - 1].type = elementType;
                this->Cells[i - 1].zone = zoneId;
                this->Cells[i - 1].parent = 0;
                this->Cells[i - 1].child = 0;
            }
        }
    }

    CHECK_HDF(H5Gclose(group));
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetFacesGlobal()
{
    hid_t group, attr;
    uint64_t firstIndex, lastIndex;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetFacesGlobal).");
    }
    attr = H5Aopen(group, "faceOffset", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetFacesGlobal).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &firstIndex));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "faceCount", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetFacesGlobal).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &lastIndex));
    CHECK_HDF(H5Aclose(attr));
    CHECK_HDF(H5Gclose(group));
    this->Faces.resize(lastIndex);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetFaces()
{
    hid_t group, attr, dset;
    uint64_t nZones;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/zoneTopology", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetFaces).");
    }
    attr = H5Aopen(group, "nZones", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetFaces).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
    CHECK_HDF(H5Aclose(attr));

    std::vector<uint64_t> minId(nZones);
    dset = H5Dopen(group, "minId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, minId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<uint64_t> maxId(nZones);
    dset = H5Dopen(group, "maxId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, maxId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> Id(nZones);
    dset = H5Dopen(group, "id", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, Id.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<uint64_t> dimension(nZones);
    dset = H5Dopen(group, "dimension", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, dimension.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> zoneT(nZones);
    dset = H5Dopen(group, "zoneType", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, zoneT.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> faceT(nZones);
    dset = H5Dopen(group, "faceType", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, faceT.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> childZoneId(nZones);
    dset = H5Dopen(group, "childZoneId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, childZoneId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> shadowZoneId(nZones);
    dset = H5Dopen(group, "shadowZoneId", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, shadowZoneId.data()));
    CHECK_HDF(H5Dclose(dset));

    std::vector<int32_t> flags(nZones);
    dset = H5Dopen(group, "flags", H5P_DEFAULT);
    if (dset < 0)
    {
        throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, flags.data()));
    CHECK_HDF(H5Dclose(dset));

    for (uint64_t iZone = 0; iZone < nZones; iZone++)
    {
        unsigned int zoneId = static_cast<unsigned int>(Id[iZone]);
        unsigned int firstIndex = static_cast<unsigned int>(minId[iZone]);
        unsigned int lastIndex = static_cast<unsigned int>(maxId[iZone]);
        // This next lines should be uncommented following test with Fluent file
        // containing tree format (AMR) and interface faces
        //// unsigned int child = static_cast<unsigned int>(childZoneId[iZone]);
        //// unsigned int shadow = static_cast<unsigned int>(shadowZoneId[iZone]);
        // next child, parent, periodicShadow variable should be initialized correctly

        for (unsigned int i = firstIndex; i <= lastIndex; i++)
        {
            this->Faces[i - 1].zone = zoneId;
            this->Faces[i - 1].periodicShadow = 0;
            this->Faces[i - 1].parent = 0;
            this->Faces[i - 1].child = 0;
            this->Faces[i - 1].interfaceFaceParent = 0;
            this->Faces[i - 1].ncgParent = 0;
            this->Faces[i - 1].ncgChild = 0;
            this->Faces[i - 1].interfaceFaceChild = 0;
        }
    }

    CHECK_HDF(H5Gclose(group));

    // FaceType
    uint64_t nSections;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/nodes", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetFaces nodes).");
    }
    attr = H5Aopen(group, "nSections", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetFaces nodes).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
    CHECK_HDF(H5Aclose(attr));
    CHECK_HDF(H5Gclose(group));

    for (uint64_t iSection = 0; iSection < nSections; iSection++)
    {
        uint64_t minId_fnodes, maxId_fnodes, nodes_size;
        std::string groupname = std::string("/meshes/1/faces/nodes/" + std::to_string(iSection + 1));
        group = H5Gopen(this->HDFImpl->FluentCaseFile, groupname.c_str(), H5P_DEFAULT);
        if (group < 0)
        {
            throw std::runtime_error("Unable to open HDF group (GetFaces nodes isection).");
        }

        attr = H5Aopen(group, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaces nodes isection).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId_fnodes));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(group, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaces nodes isection).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId_fnodes));
        CHECK_HDF(H5Aclose(attr));

        std::vector<int16_t> nnodes_fnodes(maxId_fnodes - minId_fnodes + 1);
        dset = H5Dopen(group, "nnodes", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetFaces nodes isection).");
        }
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, nnodes_fnodes.data()));
        CHECK_HDF(H5Dclose(dset));

        dset = H5Dopen(group, "nodes", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetFaces nodes isection).");
        }
        hid_t space = H5Dget_space(dset);
        hid_t ndims = H5Sget_simple_extent_ndims(space);
        if (ndims < 1)
        {
            throw std::runtime_error("Unable to open HDF group (GetFaces ndims < 1).");
        }
        std::vector<hsize_t> dims(ndims);
        CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
        nodes_size = static_cast<uint64_t>(dims[0]);

        std::vector<uint32_t> nodes_fnodes(nodes_size);
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, nodes_fnodes.data()));
        CHECK_HDF(H5Dclose(dset));

        int numberOfNodesInFace = 0;
        uint64_t ptr = minId_fnodes;
        for (unsigned int i = static_cast<unsigned int>(minId_fnodes);
            i <= static_cast<unsigned int>(maxId_fnodes); i++)
        {
            numberOfNodesInFace = static_cast<int>(nnodes_fnodes[i - minId_fnodes]);

            this->Faces[i - 1].nodes.resize(numberOfNodesInFace);
            this->Faces[i - 1].type = numberOfNodesInFace;

            for (int k = 0; k < numberOfNodesInFace; k++)
            {
                this->Faces[i - 1].nodes[k] = static_cast<int>(nodes_fnodes[ptr - 1]) - 1;
                ptr++;
            }
        }
        CHECK_HDF(H5Gclose(group));
    }

    // C0 C1
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/c0", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetFaces c0).");
    }
    attr = H5Aopen(group, "nSections", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetFaces c0).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
    CHECK_HDF(H5Aclose(attr));
    for (uint64_t iSection = 0; iSection < nSections; iSection++)
    {
        uint64_t minc0, maxc0;

        dset = H5Dopen(group, std::to_string(iSection + 1).c_str(), H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetFaces c0 iSection).");
        }
        attr = H5Aopen(dset, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaces c0 iSection).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minc0));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(dset, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaces c0 iSection).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxc0));
        CHECK_HDF(H5Aclose(attr));

        std::vector<uint32_t> c0(maxc0 - minc0 + 1);
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, c0.data()));
        CHECK_HDF(H5Dclose(dset));

        for (unsigned int i = static_cast<unsigned int>(minc0); i <= static_cast<unsigned int>(maxc0);
            i++)
        {
            this->Faces[i - 1].c0 = static_cast<int>(c0[i - minc0]) - 1;
            if (this->Faces[i - 1].c0 >= 0)
            {
                this->Cells[this->Faces[i - 1].c0].faces.push_back(i - 1);
            }
        }
    }
    CHECK_HDF(H5Gclose(group));

    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/c1", H5P_DEFAULT);
    if (group < 0)
    {
        throw std::runtime_error("Unable to open HDF group (GetFaces c1).");
    }
    attr = H5Aopen(group, "nSections", H5P_DEFAULT);
    if (attr < 0)
    {
        throw std::runtime_error("Unable to open HDF attribute (GetFaces c1).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
    CHECK_HDF(H5Aclose(attr));
    for (std::size_t i = 0; i < this->Faces.size(); i++)
    {
        this->Faces[i].c1 = -1;
    }
    for (uint64_t iSection = 0; iSection < nSections; iSection++)
    {
        uint64_t minc1, maxc1;

        dset = H5Dopen(group, std::to_string(iSection + 1).c_str(), H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetFaces c1 iSection).");
        }
        attr = H5Aopen(dset, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaces c1 iSection).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minc1));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(dset, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaces c1 iSection).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxc1));
        CHECK_HDF(H5Aclose(attr));

        std::vector<uint32_t> c1(maxc1 - minc1 + 1);
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, c1.data()));
        CHECK_HDF(H5Dclose(dset));

        for (unsigned int i = static_cast<unsigned int>(minc1); i <= static_cast<unsigned int>(maxc1);
            i++)
        {
            this->Faces[i - 1].c1 = static_cast<int>(c1[i - minc1]) - 1;
            if (this->Faces[i - 1].c1 >= 0)
            {
                this->Cells[this->Faces[i - 1].c1].faces.push_back(i - 1);
            }
        }
    }

    CHECK_HDF(H5Gclose(group));
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCellTree()
{
    herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/tree", false, nullptr);
    if (s1 == 0)
    {
        hid_t group, attr, dset;
        uint64_t minId, maxId;
        group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/tree/1", H5P_DEFAULT);
        if (group < 0)
        {
            throw std::runtime_error("Unable to open HDF group (GetCellTree).");
        }
        attr = H5Aopen(group, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetCellTree).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(group, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetCellTree).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId));
        CHECK_HDF(H5Aclose(attr));

        std::vector<int16_t> nkids(maxId - minId + 1);
        dset = H5Dopen(group, "nkids", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetCellTree).");
        }
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, nkids.data()));
        CHECK_HDF(H5Dclose(dset));

        uint64_t kids_size;
        dset = H5Dopen(group, "kids", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetCellTree).");
        }
        hid_t space = H5Dget_space(dset);
        hid_t ndims = H5Sget_simple_extent_ndims(space);
        if (ndims < 1)
        {
            throw std::runtime_error("Unable to open HDF group (GetCellTree ndims < 1).");
        }
        std::vector<hsize_t> dims(ndims);
        CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
        kids_size = static_cast<uint64_t>(dims[0]);

        std::vector<uint32_t> kids(kids_size);
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, kids.data()));
        CHECK_HDF(H5Dclose(dset));

        uint64_t ptr = 0;
        for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
            i++)
        {
            this->Cells[i - 1].parent = 1;
            int numberOfKids = static_cast<int>(nkids[i - minId]);
            this->Cells[i - 1].childId.resize(numberOfKids);
            for (int j = 0; j < numberOfKids; j++)
            {
                this->Cells[kids[ptr] - 1].child = 1;
                this->Cells[i - 1].childId[j] = kids[ptr] - 1;
                ptr++;
            }
        }

        CHECK_HDF(H5Gclose(group));
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetFaceTree()
{
    herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/tree", false, nullptr);
    if (s1 == 0)
    {
        hid_t group, attr, dset;
        uint64_t minId, maxId;
        group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/tree/1", H5P_DEFAULT);
        if (group < 0)
        {
            throw std::runtime_error("Unable to open HDF group (GetFaceTree).");
        }
        attr = H5Aopen(group, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaceTree).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(group, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetFaceTree).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId));
        CHECK_HDF(H5Aclose(attr));

        std::vector<int16_t> nkids(maxId - minId + 1);
        dset = H5Dopen(group, "nkids", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetFaceTree).");
        }
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, nkids.data()));
        CHECK_HDF(H5Dclose(dset));

        uint64_t kids_size;
        dset = H5Dopen(group, "kids", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetFaceTree).");
        }
        hid_t space = H5Dget_space(dset);
        hid_t ndims = H5Sget_simple_extent_ndims(space);
        if (ndims < 1)
        {
            throw std::runtime_error("Unable to open HDF group (GetFaceTree ndims < 1).");
        }
        std::vector<hsize_t> dims(ndims);
        CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
        kids_size = static_cast<uint64_t>(dims[0]);

        std::vector<uint32_t> kids(kids_size);
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, kids.data()));
        CHECK_HDF(H5Dclose(dset));

        uint64_t ptr = 0;
        for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
            i++)
        {
            this->Faces[i - 1].parent = 1;
            int numberOfKids = static_cast<int>(nkids[i - minId]);
            for (int j = 0; j < numberOfKids; j++)
            {
                this->Faces[kids[ptr] - 1].child = 1;
                ptr++;
            }
        }

        CHECK_HDF(H5Gclose(group));
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetInterfaceFaceParents()
{
    herr_t s1 =
        H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/interface", false, nullptr);
    if (s1 == 0)
    {
        hid_t group, attr, dset;
        uint64_t nData, nZones;
        group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/interface", H5P_DEFAULT);
        if (group < 0)
        {
            throw std::runtime_error("Unable to open HDF group (GetInterfaceFaceParents).");
        }
        attr = H5Aopen(group, "nData", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetInterfaceFaceParents).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nData));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(group, "nZones", H5P_DEFAULT);
        if (attr < 0)
        {
            throw std::runtime_error("Unable to open HDF attribute (GetInterfaceFaceParents).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
        CHECK_HDF(H5Aclose(attr));

        std::vector<uint64_t> nciTopology(nData * nZones);
        dset = H5Dopen(group, "nciTopology", H5P_DEFAULT);
        if (dset < 0)
        {
            throw std::runtime_error("Unable to open HDF dataset (GetInterfaceFaceParents).");
        }
        CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, nciTopology.data()));
        CHECK_HDF(H5Dclose(dset));

        for (uint64_t iZone = 0; iZone < nZones; iZone++)
        {
            int zoneId = static_cast<int>(nciTopology[iZone * nData]);
            int minId = static_cast<int>(nciTopology[iZone * nData + 1]);
            int maxId = static_cast<int>(nciTopology[iZone * nData + 2]);

            hid_t group_int = H5Gopen(group, std::to_string(zoneId).c_str(), H5P_DEFAULT);
            if (group_int < 0)
            {
                throw std::runtime_error("Unable to open HDF group (GetInterfaceFaceParents topology).");
            }
            std::vector<uint64_t> pf0(maxId - minId + 1);
            std::vector<uint64_t> pf1(maxId - minId + 1);
            dset = H5Dopen(group_int, "pf0", H5P_DEFAULT);
            if (dset < 0)
            {
                throw std::runtime_error("Unable to open HDF dataset (GetInterfaceFaceParents topology).");
            }
            CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, pf0.data()));
            CHECK_HDF(H5Dclose(dset));
            dset = H5Dopen(group_int, "pf1", H5P_DEFAULT);
            if (dset < 0)
            {
                throw std::runtime_error("Unable to open HDF dataset (GetInterfaceFaceParents topology).");
            }
            CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, pf1.data()));
            CHECK_HDF(H5Dclose(dset));

            for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
                i++)
            {
                unsigned int parentId0 = static_cast<unsigned int>(pf0[i - minId]);
                unsigned int parentId1 = static_cast<unsigned int>(pf1[i - minId]);

                this->Faces[parentId0 - 1].interfaceFaceParent = 1;
                this->Faces[parentId1 - 1].interfaceFaceParent = 1;
                this->Faces[i - 1].interfaceFaceChild = 1;
            }
            CHECK_HDF(H5Gclose(group_int));
        }

        CHECK_HDF(H5Gclose(group));
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::CleanCells()
{
    std::vector<int> t;
    for (auto& cell : this->Cells)
    {

        if (((cell.type == 1) && (cell.faces.size() != 3)) ||
            ((cell.type == 2) && (cell.faces.size() != 4)) ||
            ((cell.type == 3) && (cell.faces.size() != 4)) ||
            ((cell.type == 4) && (cell.faces.size() != 6)) ||
            ((cell.type == 5) && (cell.faces.size() != 5)) ||
            ((cell.type == 6) && (cell.faces.size() != 5)))
        {

            // Copy faces
            t.clear();
            for (std::size_t j = 0; j < cell.faces.size(); j++)
            {
                t.push_back(cell.faces[j]);
            }

            // Clear Faces
            cell.faces.clear();

            // Copy the faces that are not flagged back into the cell
            for (std::size_t j = 0; j < t.size(); j++)
            {
                if ((this->Faces[t[j]].child == 0) && (this->Faces[t[j]].ncgChild == 0) &&
                    (this->Faces[t[j]].interfaceFaceChild == 0))
                {
                    cell.faces.push_back(t[j]);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateCellTree()
{
    for (const auto& cell : this->Cells)
    {
        // If cell is parent cell -> interpolate data from children
        if (cell.parent == 1)
        {
            for (auto& dataChunk : this->DataChunks)
            {
                for (std::size_t k = 0; k < dataChunk.dim; k++)
                {
                    double data = 0.0;
                    int ncell = 0;
                    for (std::size_t j = 0; j < cell.childId.size(); j++)
                    {
                        if (this->Cells[cell.childId[j]].parent == 0)
                        {
                            data += dataChunk.dataVector[k + dataChunk.dim * cell.childId[j]];
                            ncell++;
                        }
                    }
                    dataChunk.dataVector.emplace_back((ncell != 0 ? data / static_cast<double>(ncell) : 0.0));
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateCellNodes()
{
    for (size_t i{ 0 }; i < this->Cells.size(); i++)
    {
        const int id{ static_cast<int>(i) };
        switch (this->Cells[i].type)
        {
        case 1: // Triangle
            this->PopulateTriangleCell(id);
            break;

        case 2: // Tetrahedron
            this->PopulateTetraCell(id);
            break;

        case 3: // Quadrilateral
            this->PopulateQuadCell(id);
            break;

        case 4: // Hexahedral
            this->PopulateHexahedronCell(id);
            break;

        case 5: // Pyramid
            this->PopulatePyramidCell(id);
            break;

        case 6: // Wedge
            this->PopulateWedgeCell(id);
            break;

        case 7: // Polyhedron
            this->PopulatePolyhedronCell(id);
            break;
        }
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateTriangleCell(int i)
{
    this->Cells[i].nodes.resize(3);
    if (this->Faces[this->Cells[i].faces[0]].c0 == i)
    {
        this->Cells[i].nodes[0] = this->Faces[this->Cells[i].faces[0]].nodes[0];
        this->Cells[i].nodes[1] = this->Faces[this->Cells[i].faces[0]].nodes[1];
    }
    else
    {
        this->Cells[i].nodes[1] = this->Faces[this->Cells[i].faces[0]].nodes[0];
        this->Cells[i].nodes[0] = this->Faces[this->Cells[i].faces[0]].nodes[1];
    }

    if (this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[0] &&
        this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[1])
    {
        this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[1]].nodes[0];
    }
    else
    {
        this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[1]].nodes[1];
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateTetraCell(int i)
{
    this->Cells[i].nodes.resize(4);

    if (this->Faces[this->Cells[i].faces[0]].c0 == i)
    {
        this->Cells[i].nodes[0] = this->Faces[this->Cells[i].faces[0]].nodes[0];
        this->Cells[i].nodes[1] = this->Faces[this->Cells[i].faces[0]].nodes[1];
        this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[0]].nodes[2];
    }
    else
    {
        this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[0]].nodes[0];
        this->Cells[i].nodes[1] = this->Faces[this->Cells[i].faces[0]].nodes[1];
        this->Cells[i].nodes[0] = this->Faces[this->Cells[i].faces[0]].nodes[2];
    }

    if (this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[0] &&
        this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[1] &&
        this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[2])
    {
        this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[1]].nodes[0];
    }
    else if (this->Faces[this->Cells[i].faces[1]].nodes[1] != this->Cells[i].nodes[0] &&
        this->Faces[this->Cells[i].faces[1]].nodes[1] != this->Cells[i].nodes[1] &&
        this->Faces[this->Cells[i].faces[1]].nodes[1] != this->Cells[i].nodes[2])
    {
        this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[1]].nodes[1];
    }
    else
    {
        this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[1]].nodes[2];
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateQuadCell(int i)
{
    this->Cells[i].nodes.resize(4);

    if (this->Faces[this->Cells[i].faces[0]].c0 == i)
    {
        this->Cells[i].nodes[0] = this->Faces[this->Cells[i].faces[0]].nodes[0];
        this->Cells[i].nodes[1] = this->Faces[this->Cells[i].faces[0]].nodes[1];
    }
    else
    {
        this->Cells[i].nodes[1] = this->Faces[this->Cells[i].faces[0]].nodes[0];
        this->Cells[i].nodes[0] = this->Faces[this->Cells[i].faces[0]].nodes[1];
    }

    if ((this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[0] &&
        this->Faces[this->Cells[i].faces[1]].nodes[0] != this->Cells[i].nodes[1]) &&
        (this->Faces[this->Cells[i].faces[1]].nodes[1] != this->Cells[i].nodes[0] &&
            this->Faces[this->Cells[i].faces[1]].nodes[1] != this->Cells[i].nodes[1]))
    {
        if (this->Faces[this->Cells[i].faces[1]].c0 == i)
        {
            this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[1]].nodes[0];
            this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[1]].nodes[1];
        }
        else
        {
            this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[1]].nodes[0];
            this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[1]].nodes[1];
        }
    }
    else if ((this->Faces[this->Cells[i].faces[2]].nodes[0] != this->Cells[i].nodes[0] &&
        this->Faces[this->Cells[i].faces[2]].nodes[0] != this->Cells[i].nodes[1]) &&
        (this->Faces[this->Cells[i].faces[2]].nodes[1] != this->Cells[i].nodes[0] &&
            this->Faces[this->Cells[i].faces[2]].nodes[1] != this->Cells[i].nodes[1]))
    {
        if (this->Faces[this->Cells[i].faces[2]].c0 == i)
        {
            this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[2]].nodes[0];
            this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[2]].nodes[1];
        }
        else
        {
            this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[2]].nodes[0];
            this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[2]].nodes[1];
        }
    }
    else
    {
        if (this->Faces[this->Cells[i].faces[3]].c0 == i)
        {
            this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[3]].nodes[0];
            this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[3]].nodes[1];
        }
        else
        {
            this->Cells[i].nodes[3] = this->Faces[this->Cells[i].faces[3]].nodes[0];
            this->Cells[i].nodes[2] = this->Faces[this->Cells[i].faces[3]].nodes[1];
        }
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateHexahedronCell(int i)
{
    this->Cells[i].nodes.resize(8);

    // Throw error when number of face of hexahedron cell is below 4.
    // Number of face should be 6 but you can find the 8 corner points with at least 4 faces.
    if (this->Cells[i].faces.size() < 4)
    {
        throw std::runtime_error("Some cells of the domain are incompatible with this reader.");
    }

    if (this->Faces[this->Cells[i].faces[0]].c0 == i)
    {
        for (int j = 0; j < 4; j++)
        {
            this->Cells[i].nodes[j] = this->Faces[this->Cells[i].faces[0]].nodes[j];
        }
    }
    else
    {
        for (int j = 3; j >= 0; j--)
        {
            this->Cells[i].nodes[3 - j] = this->Faces[this->Cells[i].faces[0]].nodes[j];
        }
    }

    //  Look for opposite face of hexahedron
    for (std::size_t j = 1; j < this->Cells[i].faces.size(); j++)
    {
        int flag = 0;
        for (int k = 0; k < 4; k++)
        {
            if ((this->Cells[i].nodes[0] == this->Faces[this->Cells[i].faces[j]].nodes[k]) ||
                (this->Cells[i].nodes[1] == this->Faces[this->Cells[i].faces[j]].nodes[k]) ||
                (this->Cells[i].nodes[2] == this->Faces[this->Cells[i].faces[j]].nodes[k]) ||
                (this->Cells[i].nodes[3] == this->Faces[this->Cells[i].faces[j]].nodes[k]))
            {
                flag = 1;
            }
        }
        if (flag == 0)
        {
            if (this->Faces[this->Cells[i].faces[j]].c1 == i)
            {
                for (int k = 4; k < 8; k++)
                {
                    this->Cells[i].nodes[k] = this->Faces[this->Cells[i].faces[j]].nodes[k - 4];
                }
            }
            else
            {
                for (int k = 7; k >= 4; k--)
                {
                    this->Cells[i].nodes[k] = this->Faces[this->Cells[i].faces[j]].nodes[7 - k];
                }
            }
        }
    }

    //  Find the face with points 0 and 1 in them.
    int f01[4] = { -1, -1, -1, -1 };
    for (std::size_t j = 1; j < this->Cells[i].faces.size(); j++)
    {
        int flag0 = 0;
        int flag1 = 0;
        for (int k = 0; k < 4; k++)
        {
            if (this->Cells[i].nodes[0] == this->Faces[this->Cells[i].faces[j]].nodes[k])
            {
                flag0 = 1;
            }
            if (this->Cells[i].nodes[1] == this->Faces[this->Cells[i].faces[j]].nodes[k])
            {
                flag1 = 1;
            }
        }
        if ((flag0 == 1) && (flag1 == 1))
        {
            if (this->Faces[this->Cells[i].faces[j]].c0 == i)
            {
                for (int k = 0; k < 4; k++)
                {
                    f01[k] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
            else
            {
                for (int k = 3; k >= 0; k--)
                {
                    f01[k] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
        }
    }

    //  Find the face with points 0 and 3 in them.
    int f03[4] = { -1, -1, -1, -1 };
    for (std::size_t j = 1; j < this->Cells[i].faces.size(); j++)
    {
        int flag0 = 0;
        int flag1 = 0;
        for (int k = 0; k < 4; k++)
        {
            if (this->Cells[i].nodes[0] == this->Faces[this->Cells[i].faces[j]].nodes[k])
            {
                flag0 = 1;
            }
            if (this->Cells[i].nodes[3] == this->Faces[this->Cells[i].faces[j]].nodes[k])
            {
                flag1 = 1;
            }
        }

        if ((flag0 == 1) && (flag1 == 1))
        {
            if (this->Faces[this->Cells[i].faces[j]].c0 == i)
            {
                for (int k = 0; k < 4; k++)
                {
                    f03[k] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
            else
            {
                for (int k = 3; k >= 0; k--)
                {
                    f03[k] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
        }
    }

    // What point is in f01 and f03 besides 0 ... this is point 4
    int p4 = 0;
    for (int k = 0; k < 4; k++)
    {
        if (f01[k] != this->Cells[i].nodes[0])
        {
            for (int n = 0; n < 4; n++)
            {
                if (f01[k] == f03[n])
                {
                    p4 = f01[k];
                }
            }
        }
    }

    // Since we know point 4 now we check to see if points
    //  4, 5, 6, and 7 are in the correct positions.
    int t[8];
    t[4] = this->Cells[i].nodes[4];
    t[5] = this->Cells[i].nodes[5];
    t[6] = this->Cells[i].nodes[6];
    t[7] = this->Cells[i].nodes[7];
    if (p4 == this->Cells[i].nodes[5])
    {
        this->Cells[i].nodes[5] = t[6];
        this->Cells[i].nodes[6] = t[7];
        this->Cells[i].nodes[7] = t[4];
        this->Cells[i].nodes[4] = t[5];
    }
    else if (p4 == Cells[i].nodes[6])
    {
        this->Cells[i].nodes[5] = t[7];
        this->Cells[i].nodes[6] = t[4];
        this->Cells[i].nodes[7] = t[5];
        this->Cells[i].nodes[4] = t[6];
    }
    else if (p4 == Cells[i].nodes[7])
    {
        this->Cells[i].nodes[5] = t[4];
        this->Cells[i].nodes[6] = t[5];
        this->Cells[i].nodes[7] = t[6];
        this->Cells[i].nodes[4] = t[7];
    }
    // else point 4 was lined up so everything was correct.
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulatePyramidCell(int i)
{
    this->Cells[i].nodes.resize(5);
    //  The quad face will be the base of the pyramid
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        if (this->Faces[this->Cells[i].faces[j]].nodes.size() == 4)
        {
            if (this->Faces[this->Cells[i].faces[j]].c0 == i)
            {
                for (int k = 0; k < 4; k++)
                {
                    this->Cells[i].nodes[k] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
            else
            {
                for (int k = 0; k < 4; k++)
                {
                    this->Cells[i].nodes[3 - k] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
        }
    }

    // Just need to find point 4
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        if (this->Faces[this->Cells[i].faces[j]].nodes.size() == 3)
        {
            for (int k = 0; k < 3; k++)
            {
                if ((this->Faces[this->Cells[i].faces[j]].nodes[k] != this->Cells[i].nodes[0]) &&
                    (this->Faces[this->Cells[i].faces[j]].nodes[k] != this->Cells[i].nodes[1]) &&
                    (this->Faces[this->Cells[i].faces[j]].nodes[k] != this->Cells[i].nodes[2]) &&
                    (this->Faces[this->Cells[i].faces[j]].nodes[k] != this->Cells[i].nodes[3]))
                {
                    this->Cells[i].nodes[4] = this->Faces[this->Cells[i].faces[j]].nodes[k];
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateWedgeCell(int i)
{
    this->Cells[i].nodes.resize(6);

    //  Find the first triangle face and make it the base.
    int base = 0;
    int first = 0;
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        if ((this->Faces[this->Cells[i].faces[j]].type == 3) && (first == 0))
        {
            base = this->Cells[i].faces[j];
            first = 1;
        }
    }

    //  Find the second triangle face and make it the top.
    int top = 0;
    int second = 0;
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        if ((this->Faces[this->Cells[i].faces[j]].type == 3) && (second == 0) &&
            (this->Cells[i].faces[j] != base))
        {
            top = this->Cells[i].faces[j];
            second = 1;
        }
    }

    // Load Base nodes into the nodes std::vector
    if (this->Faces[base].c0 == i)
    {
        for (int j = 0; j < 3; j++)
        {
            this->Cells[i].nodes[j] = this->Faces[base].nodes[j];
        }
    }
    else
    {
        for (int j = 2; j >= 0; j--)
        {
            this->Cells[i].nodes[2 - j] = this->Faces[base].nodes[j];
        }
    }
    // Load Top nodes into the nodes std::vector
    if (this->Faces[top].c1 == i)
    {
        for (int j = 3; j < 6; j++)
        {
            this->Cells[i].nodes[j] = this->Faces[top].nodes[j - 3];
        }
    }
    else
    {
        for (int j = 3; j < 6; j++)
        {
            this->Cells[i].nodes[j] = this->Faces[top].nodes[5 - j];
        }
    }

    //  Find the quad face with points 0 and 1 in them.
    int w01[4] = { -1, -1, -1, -1 };
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        if (this->Cells[i].faces[j] != base && this->Cells[i].faces[j] != top)
        {
            int wf0 = 0;
            int wf1 = 0;
            for (int k = 0; k < 4; k++)
            {
                if (this->Cells[i].nodes[0] == this->Faces[this->Cells[i].faces[j]].nodes[k])
                {
                    wf0 = 1;
                }
                if (this->Cells[i].nodes[1] == this->Faces[this->Cells[i].faces[j]].nodes[k])
                {
                    wf1 = 1;
                }
                if ((wf0 == 1) && (wf1 == 1))
                {
                    for (int n = 0; n < 4; n++)
                    {
                        w01[n] = this->Faces[this->Cells[i].faces[j]].nodes[n];
                    }
                }
            }
        }
    }

    //  Find the quad face with points 0 and 2 in them.
    int w02[4] = { -1, -1, -1, -1 };
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        if (this->Cells[i].faces[j] != base && this->Cells[i].faces[j] != top)
        {
            int wf0 = 0;
            int wf2 = 0;
            for (int k = 0; k < 4; k++)
            {
                if (this->Cells[i].nodes[0] == this->Faces[this->Cells[i].faces[j]].nodes[k])
                {
                    wf0 = 1;
                }
                if (this->Cells[i].nodes[2] == this->Faces[this->Cells[i].faces[j]].nodes[k])
                {
                    wf2 = 1;
                }
                if ((wf0 == 1) && (wf2 == 1))
                {
                    for (int n = 0; n < 4; n++)
                    {
                        w02[n] = this->Faces[this->Cells[i].faces[j]].nodes[n];
                    }
                }
            }
        }
    }

    // Point 3 is the point that is in both w01 and w02

    // What point is in f01 and f02 besides 0 ... this is point 3
    int p3 = 0;
    for (int k = 0; k < 4; k++)
    {
        if (w01[k] != this->Cells[i].nodes[0])
        {
            for (int n = 0; n < 4; n++)
            {
                if (w01[k] == w02[n])
                {
                    p3 = w01[k];
                }
            }
        }
    }

    // Since we know point 3 now we check to see if points
    //  3, 4, and 5 are in the correct positions.
    int t[6];
    t[3] = this->Cells[i].nodes[3];
    t[4] = this->Cells[i].nodes[4];
    t[5] = this->Cells[i].nodes[5];
    if (p3 == this->Cells[i].nodes[4])
    {
        this->Cells[i].nodes[3] = t[4];
        this->Cells[i].nodes[4] = t[5];
        this->Cells[i].nodes[5] = t[3];
    }
    else if (p3 == this->Cells[i].nodes[5])
    {
        this->Cells[i].nodes[3] = t[5];
        this->Cells[i].nodes[4] = t[3];
        this->Cells[i].nodes[5] = t[4];
    }
    // else point 3 was lined up so everything was correct.
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulatePolyhedronCell(int i)
{
    // Reconstruct polyhedron cell for VTK
    // For polyhedron cell, a special ptIds input format is used:
    // nodes stores the nodeIds while nodesOffset stores the node Offset for each faces
    int currentOffset = 0;
    this->Cells[i].nodesOffset.push_back(currentOffset);
    for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
    {
        std::size_t numFacePts = this->Faces[this->Cells[i].faces[j]].nodes.size();
        if (numFacePts != 0)
        {
            currentOffset += static_cast<int>(numFacePts);
            this->Cells[i].nodesOffset.push_back(currentOffset);
            for (std::size_t k = 0; k < numFacePts; k++)
            {
                this->Cells[i].nodes.push_back(this->Faces[this->Cells[i].faces[j]].nodes[k]);
            }
        }
    }
}
