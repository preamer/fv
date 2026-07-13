// A modified version of the original vtkFLUENTCFFReader.h

#pragma once

#include <pybind11/pybind11.h>
#include <string>
#include <memory>
#include <vector>

class vtkFLUENTCFFReader
{
public:
    vtkFLUENTCFFReader();
    ~vtkFLUENTCFFReader();

    pybind11::dict ReadMeshData(const std::string& filename);
    pybind11::object ReadPyVistaMesh(const std::string& filename);

    struct Cell
    {
        int type;
        int zone;
        std::vector<int> faces;
        int parent;
        int child;
        std::vector<int> nodes;
        std::vector<int> nodesOffset;
        std::vector<int> childId;
    };

    struct Face
    {
        int type;
        unsigned int zone;
        std::vector<int> nodes;
        int c0;
        int c1;
        int periodicShadow;
        int parent;
        int child;
        int interfaceFaceParent;
        int interfaceFaceChild;
        int ncgParent;
        int ncgChild;
    };

    /**
     * Enumerate
     */
    enum DataState
    {
        NOT_LOADED = 0,
        AVAILABLE = 1,
        LOADED = 2,
        ERROR = 3
    };

    //@{
    /**
     * Open the HDF5 file structure
     */
    virtual bool OpenCaseFile(const std::string& filename);
    virtual DataState OpenDataFile(const std::string& filename);
    //@}

    /**
     * Retrieve the number of cell zones
     */
    virtual void GetNumberOfCellZones();

    /**
     * Reads necessary information from the .cas file
     */
    virtual int ParseCaseFile();

    /**
     * Get the dimension of the file (2D/3D)
     */
    virtual int GetDimension();

    //@{
    /**
     * Get the total number of nodes/cells/faces
     */
    virtual void GetNodesGlobal();
    virtual void GetCellsGlobal();
    virtual void GetFacesGlobal();
    //@}

    /**
     * Get the size and index of node per zone
     */
    virtual void GetNodes();

    /**
     * Get the topology of cell per zone
     */
    virtual void GetCells();

    /**
     * Get the topology of face per zone
     */
    virtual void GetFaces();

    /**
     * Get the periodic shadown faces information
     * !!! NOT IMPLEMENTED YET !!!
     */
    virtual void GetPeriodicShadowFaces();

    /**
     * Get the overset cells information
     * !!! NOT IMPLEMENTED YET !!!
     */
    virtual void GetCellOverset();

    /**
     * Get the tree (AMR) cell topology
     */
    virtual void GetCellTree();

    /**
     * Get the tree (AMR) face topology
     */
    virtual void GetFaceTree();

    /**
     * Get the interface id of parent faces
     */
    virtual void GetInterfaceFaceParents();

    /**
     * Get the non conformal grid interface information
     * !!! NOT IMPLEMENTED YET !!!
     */
    virtual void GetNonconformalGridInterfaceFaceInformation();

    /**
     * Removes unnecessary faces from the cells
     */
    virtual void CleanCells();

    //@{
    /**
     * Reconstruct and convert the Fluent data format
     * to the VTK format
     */
    virtual void PopulateCellNodes();
    virtual void PopulateCellTree();
    //@}

    //@{
    /**
     * Reconstruct VTK cell topology from Fluent format
     */
    virtual void PopulateTriangleCell(int i);
    virtual void PopulateTetraCell(int i);
    virtual void PopulateQuadCell(int i);
    virtual void PopulateHexahedronCell(int i);
    virtual void PopulatePyramidCell(int i);
    virtual void PopulateWedgeCell(int i);
    virtual void PopulatePolyhedronCell(int i);
    //@}

    /**
     * Read and reconstruct data from .dat.h5 file
     */
    virtual int GetData();

    /**
     * Pre-read variable name data available for selection
     */
    virtual int GetMetaData();

    //
    //  Variables
    //
    std::string FileName;
    bool RenameArrays = false;

    struct vtkInternals;
    std::unique_ptr<vtkInternals> HDFImpl;

    std::vector<double> Points;
    // vtkNew<vtkTriangle> Triangle;
    // vtkNew<vtkTetra> Tetra;
    // vtkNew<vtkQuad> Quad;
    // vtkNew<vtkHexahedron> Hexahedron;
    // vtkNew<vtkPyramid> Pyramid;
    // vtkNew<vtkWedge> Wedge;

    std::vector<Cell> Cells;
    std::vector<Face> Faces;
    std::vector<int> CellZones;

    int GridDimension = 0;
    DataState FileState = DataState::NOT_LOADED;

private:
    vtkFLUENTCFFReader(const vtkFLUENTCFFReader&) = delete;
    void operator=(const vtkFLUENTCFFReader&) = delete;

    struct DataChunk
    {
        std::string variableName;
        int zoneId;
        size_t dim;
        std::vector<double> dataVector;
    };

    std::vector<DataChunk> DataChunks;
    std::vector<std::string> PreReadData;
    int NumberOfArrays = 0;
};
