[中文](README.zh.md)

# cffview

A command-line tool for inspecting Ansys Fluent `.cas.h5` / `.msh.h5` files **without opening Fluent**.

- Read solver settings, materials, boundary conditions, discretisation schemes, and more directly from the HDF5 file.
- Visualise the mesh with [PyVista](https://pyvista.org).
- Mesh reader based on a modified version of VTK's [`vtkFLUENTCFFReader`](https://github.com/Kitware/VTK/tree/master/IO/FLUENTCFF), with the VTK dependency removed. Supports all VTK cell types including polyhedra, for both `.cas.h5` and `.msh.h5`.

---

## Installation

```bash
pip install cffview
```

### Build from source

HDF5 development headers and libraries are required.

```bash
git clone https://github.com/preamer/cffview.git
cd cffview
pip install .
```

If auto-detection fails, set `HDF5_DIR` explicitly before installing.

---

## Usage

```
cffview <file> [options]
```

### Options

| Option | Description |
|---|---|
| `-v`, `--version` | Print the Fluent version stored in the file |
| `--extract` | Dump raw Scheme settings to `general.scm` and `boundary.scm` |
| `--showmesh` | Visualise the mesh interactively with PyVista |
| `--solver` | Solver type, time, dimension, precision, turbulence model, energy, radiation, gravity |
| `--mat` | Material properties |
| `--bd` | Boundary condition settings |
| `--ne` | Named expressions |
| `--disc` | Discretisation schemes and relaxation factors |
| `--rd` | Report definitions |
| `--plotsets` | Plot sets |
| `--monitorsets` | Monitor sets |
| `--iter` | Iteration / time-step settings |
| `--save` | Save the output to `<file>.json` |

Multiple flags can be combined freely. Case settings flags (`--solver`, `--mat`, etc.) apply to `.cas.h5` files only.

### Examples

```bash
# Show solver configuration and boundary conditions
cffview case.cas.h5 --solver --bd

# Show all settings and save to JSON
cffview case.cas.h5 --solver --mat --bd --ne --disc --rd --iter --save

# Visualise the mesh (.cas.h5 and .msh.h5 both supported)
cffview case.cas.h5 --showmesh
cffview mesh.msh.h5 --showmesh

# Check the Fluent version embedded in the file
cffview case.cas.h5 --version

# Extract raw Scheme strings for manual inspection
cffview case.cas.h5 --extract
```

---

## File Format Support

| Feature | `.cas.h5` | `.msh.h5` |
|---|---|---|
| Case settings (`--solver`, `--mat`, `--bd`, …) | ✅ | — |
| Mesh visualisation (3D) | ✅ | ✅ |
| Mesh visualisation (2D) | ✅ | ⚠️ partial |

> **Note:** 2D `.msh.h5` face connectivity (C0/C1) parsing is not yet fully implemented.

---

## Supported Settings

| Flag | Contents |
|---|---|
| `--solver` | Algorithm (PBNS/DBNS), steady/transient, 2D/3D, single/double precision, turbulence model, energy, radiation, gravity |
| `--mat` | Fluid/solid materials with properties and evaluation methods |
| `--bd` | Velocity-inlet, pressure-outlet, mass-flow-inlet/outlet, wall (thermal & motion BC), porous-jump, interior, symmetry, … |
| `--ne` | Named expressions defined in the case |
| `--disc` | Per-equation scheme (Second Order Upwind, QUICK, …) and under-relaxation / pseudo-transient factors |
| `--rd` | Report definitions (volume, surface, flux) with field, zones, and per-zone flag |
| `--plotsets` | Plot set configurations |
| `--monitorsets` | Monitor set configurations |
| `--iter` | Iteration count (steady) or time-step size, number of steps, max inner iterations (transient) |

---

## License

[BSD-3-Clause](LICENSE)
